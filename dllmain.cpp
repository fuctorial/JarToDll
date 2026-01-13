// C:\gemini_Try\dllmain.cpp

#include <windows.h>
#include <jni.h>
#include <stdio.h> 
#include "classes.h"
#include "resources.h" 
#include <string.h>
#include "utils.h"

// =================================================================================
// == Global variables to store state for unloading
// == Declared at the top of the file to be visible everywhere.
// =================================================================================
JavaVM* g_jvm = nullptr;
jobject g_javaInjectorInstance = nullptr;
jobject g_originalClassLoadersMap = nullptr;
HMODULE g_hModule = NULL;

// --- НОВЫЕ ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ДЛЯ КЛАССОВ ---
jclass g_unloadCallbackClass = nullptr;
jclass g_javaInjectorClass = nullptr;
jclass g_threadClass = nullptr;
jclass g_mapClass = nullptr;
jclass g_setClass = nullptr;
jclass g_iteratorClass = nullptr;
jclass g_entryClass = nullptr;
// =================================================================================
// == Forward declarations for functions
// == This tells the compiler that these functions exist before it sees their implementation.
// =================================================================================
void CleanupAndExit();
jobject CreateResourceMapInJava(JNIEnv* env, FILE* log);

// =================================================================================
extern "C" __declspec(dllexport) DWORD WINAPI OnUnload(LPVOID lpParam) {
    FILE *log = fopen("C:\\FuctorizeUnload.log", "w");
    if (!log) log = stdout;

    fprintf(log, "[UNLOAD] OnUnload thread started.\n");
    fflush(log);

    if (!g_jvm) {
        fprintf(log, "[UNLOAD][FATAL] g_jvm is NULL.\n");
        if (log != stdout) fclose(log);
        return 1;
    }

    JNIEnv* env = nullptr;
    if (g_jvm->AttachCurrentThreadAsDaemon((void**)&env, nullptr) != JNI_OK || !env) {
        fprintf(log, "[UNLOAD][FATAL] AttachCurrentThreadAsDaemon failed.\n");
        if (log != stdout) fclose(log);
        return 1;
    }
    
    fprintf(log, "[UNLOAD] Attached. Using cached Java classes...\n");
    fflush(log);
    
    // 1. ПОЛУЧАЕМ JAVA INJECTOR И ВЫЗЫВАЕМ SHUTDOWN
    if (g_javaInjectorClass) {
        jfieldID instanceField = env->GetStaticFieldID(g_javaInjectorClass, "instance", "Lru/fuctorial/fuctorize/JavaInjector;");
        if (instanceField) {
            jobject javaInjectorInstance = env->GetStaticObjectField(g_javaInjectorClass, instanceField);
            if (javaInjectorInstance) {
                jmethodID shutdownMethod = env->GetMethodID(g_javaInjectorClass, "shutdown", "()V");
                if (shutdownMethod) {
                    env->CallVoidMethod(javaInjectorInstance, shutdownMethod);
                    fprintf(log, "[UNLOAD] Java shutdown() called.\n");
                    fflush(log);
                }
            } else {
                fprintf(log, "[UNLOAD][WARN] JavaInjector.instance is NULL. Already unloaded?\n");
            }
        }
    } else {
        fprintf(log, "[UNLOAD][FATAL] g_javaInjectorClass was not cached!\n");
    }
    
    // 2. ЖДЕМ СИГНАЛ О ЗАВЕРШЕНИИ ОЧИСТКИ ОТ JAVA
    if (g_unloadCallbackClass) {
        jmethodID isCompleteMethod = env->GetStaticMethodID(g_unloadCallbackClass, "isCleanupComplete", "()Z");
        if (isCompleteMethod) {
            int waitCycles = 0;
            while (waitCycles < 1500 && !env->CallStaticBooleanMethod(g_unloadCallbackClass, isCompleteMethod)) {
                Sleep(10);
                waitCycles++;
            }
            if(waitCycles < 1500) fprintf(log, "[UNLOAD] Java cleanup signal received.\n");
            else fprintf(log, "[UNLOAD][WARN] Timed out waiting for Java cleanup signal.\n");
            fflush(log);
        }
    } else {
        fprintf(log, "[UNLOAD][WARN] g_unloadCallbackClass not cached, skipping wait.\n");
    }

    // 3. ВОССТАНАВЛИВАЕМ CLASSLOADER'Ы
    if (g_originalClassLoadersMap && g_mapClass && g_setClass && g_iteratorClass && g_entryClass && g_threadClass) {
        fprintf(log, "[UNLOAD] Restoring original class loaders...\n");
        fflush(log);
        
        jmethodID entrySetMethod = env->GetMethodID(g_mapClass, "entrySet", "()Ljava/util/Set;");
        jobject entrySet = env->CallObjectMethod(g_originalClassLoadersMap, entrySetMethod);
        jmethodID iteratorMethod = env->GetMethodID(g_setClass, "iterator", "()Ljava/util/Iterator;");
        jobject iterator = env->CallObjectMethod(entrySet, iteratorMethod);
        jmethodID hasNextMethod = env->GetMethodID(g_iteratorClass, "hasNext", "()Z");
        jmethodID nextMethod = env->GetMethodID(g_iteratorClass, "next", "()Ljava/lang/Object;");
        jmethodID getKeyMethod = env->GetMethodID(g_entryClass, "getKey", "()Ljava/lang/Object;");
        jmethodID getValueMethod = env->GetMethodID(g_entryClass, "getValue", "()Ljava/lang/Object;");
        jmethodID setContextClassLoaderMethod = env->GetMethodID(g_threadClass, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");

        int restoredCount = 0;
        while (env->CallBooleanMethod(iterator, hasNextMethod)) {
            jobject entry = env->CallObjectMethod(iterator, nextMethod);
            jobject thread = env->CallObjectMethod(entry, getKeyMethod);
            jobject loader = env->CallObjectMethod(entry, getValueMethod);
            if (thread && setContextClassLoaderMethod) {
                env->CallVoidMethod(thread, setContextClassLoaderMethod, loader);
                restoredCount++;
            }
            if(thread) env->DeleteGlobalRef(thread);
            if(loader) env->DeleteGlobalRef(loader);
            env->DeleteLocalRef(entry);
        }
        
        env->DeleteGlobalRef(g_originalClassLoadersMap);
        g_originalClassLoadersMap = nullptr;
        fprintf(log, "[UNLOAD] Restored %d class loaders.\n", restoredCount);
        fflush(log);
    }
    
    // 4. ЗАВЕРШАЕМ
    g_jvm->DetachCurrentThread();
    fprintf(log, "[UNLOAD] Detached from JVM. C++ thread will now exit.\n");
    fflush(log);
    
    if (log != stdout) fclose(log);

    return 0;
}

// =================================================================================
// ФУНКЦИЯ: Создание HashMap в Java для хранения ресурсов в памяти
// =================================================================================


DWORD WINAPI InjectThread(LPVOID param) {
    HMODULE hModule = (HMODULE)param;

    char dllPath[MAX_PATH];
    GetModuleFileNameA(hModule, dllPath, MAX_PATH);
    char logPath[MAX_PATH];
    strcpy(logPath, dllPath);
    char* lastSlash = strrchr(logPath, '\\');
    if (lastSlash) {
        *(lastSlash + 1) = '\0';
        strcat(logPath, "inject.log");
    } else {
        strcpy(logPath, "inject.log");
    }

    FILE *log = fopen(logPath, "w");
    if (!log) return 1;
    fprintf(log, "InjectThread started\n");
    fflush(log);

    // jvm.dll via PEB (stealth)
    HMODULE jvmModule = GetModuleHandlePeb(L"jvm.dll");
    if (!jvmModule) {
        fprintf(log, "Failed: No jvm.dll found\n");
        fflush(log);
        fclose(log);
        return 1;
    }
    fprintf(log, "Got jvmModule\n");
    fflush(log);

    // Get JNI_GetCreatedJavaVMs (no unhook)
    PVOID originalGetVMs = GetProcAddressPeb(jvmModule, "JNI_GetCreatedJavaVMs");
    if (!originalGetVMs) {
        fprintf(log, "Failed: No JNI_GetCreatedJavaVMs found\n");
        fflush(log);
        fclose(log);
        return 1;
    }
    fprintf(log, "Got originalGetVMs\n");
    fflush(log);


    typedef jint(JNICALL* GetCreatedJavaVMsProc)(JavaVM**, jsize, jsize*);
    GetCreatedJavaVMsProc getVMs = (GetCreatedJavaVMsProc)originalGetVMs;
    
    jsize vmCount = 0;
    // FIXED: Store the found JVM in the global variable
    jint res = getVMs(&g_jvm, 1, &vmCount);
    if (res != JNI_OK || !g_jvm || vmCount < 1) { /* ... error handling ... */ fclose(log); return 1; }
    fprintf(log, "Got JVM, vmCount: %d\n", vmCount); fflush(log);

    JNIEnv* env = nullptr;
    // FIXED: Use the global g_jvm to attach
    jint attachResult = g_jvm->AttachCurrentThreadAsDaemon((void**)&env, nullptr);
    if (attachResult != JNI_OK || !env) { /* ... error handling ... */ fclose(log); return 1; }
    fprintf(log, "Attached thread as daemon, got env\n"); fflush(log);

    jclass threadClass = env->FindClass("java/lang/Thread");
    if (!threadClass || env->ExceptionCheck()) { /* ... error handling ... */ g_jvm->DetachCurrentThread(); fclose(log); return 1; }
    fprintf(log, "Got threadClass\n"); fflush(log);
    
    jmethodID getAllThreads = env->GetStaticMethodID(threadClass, "getAllStackTraces", "()Ljava/util/Map;");
    jobject threadMap = env->CallStaticObjectMethod(threadClass, getAllThreads);
    
    // FIXED: Use a different variable name to avoid redeclaration
    jclass mapClass_local = env->FindClass("java/util/Map");
    jmethodID keySet = env->GetMethodID(mapClass_local, "keySet", "()Ljava/util/Set;");
    jobject threadsSet = env->CallObjectMethod(threadMap, keySet);
    
    jclass setClass = env->FindClass("java/util/Set");
    jmethodID toArray = env->GetMethodID(setClass, "toArray", "()[Ljava/lang/Object;");
    jobjectArray threads = (jobjectArray)env->CallObjectMethod(threadsSet, toArray);

    jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
    if (!classLoaderClass || env->ExceptionCheck()) { /* ... error handling ... */ g_jvm->DetachCurrentThread(); fclose(log); return 1; }
    fprintf(log, "Got classLoaderClass\n"); fflush(log);

    // FIXED: Use a different variable name to avoid redeclaration
    jmethodID getContextClassLoaderMethod = env->GetMethodID(threadClass, "getContextClassLoader", "()Ljava/lang/ClassLoader;");

    jobject launchLoader = nullptr;
    for (jsize i = 0; i < env->GetArrayLength(threads); ++i) {
        jobject thread = env->GetObjectArrayElement(threads, i);
        jobject loader = env->CallObjectMethod(thread, getContextClassLoaderMethod);
        if (loader) {
            jmethodID toString = env->GetMethodID(classLoaderClass, "toString", "()Ljava/lang/String;");
            jstring nameStr = (jstring)env->CallObjectMethod(loader, toString);
            const char* name = env->GetStringUTFChars(nameStr, nullptr);
            if (strstr(name, "LaunchClassLoader")) {
                launchLoader = loader;
                env->ReleaseStringUTFChars(nameStr, name);
                break;
            }
            env->ReleaseStringUTFChars(nameStr, name);
        }
    }
    if (!launchLoader) {
        fprintf(log, "Failed: No LaunchClassLoader found\n");
        fflush(log);
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Found LaunchClassLoader\n");
    fflush(log);

    // =================================================================================
    // == NEW LOGIC: Store original ClassLoaders before overwriting them
    // =================================================================================
    // FIXED: Use a different variable name here
    jclass hashMapClass = env->FindClass("java/util/HashMap");
    jmethodID mapCtor = env->GetMethodID(hashMapClass, "<init>", "()V");
    jobject localOriginalsMap = env->NewObject(hashMapClass, mapCtor);
    g_originalClassLoadersMap = env->NewGlobalRef(localOriginalsMap);
    env->DeleteLocalRef(localOriginalsMap);

    jmethodID putMethod = env->GetMethodID(hashMapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    
    fprintf(log, "Saving original context class loaders...\n"); fflush(log);
    jsize threadCountForSave = env->GetArrayLength(threads);
    for (jsize i = 0; i < threadCountForSave; ++i) {
        jobject threadObj = env->GetObjectArrayElement(threads, i);
        jobject originalLoader = env->CallObjectMethod(threadObj, getContextClassLoaderMethod);
        
        jobject threadGlobalRef = env->NewGlobalRef(threadObj);
        jobject loaderGlobalRef = originalLoader ? env->NewGlobalRef(originalLoader) : NULL;

        env->CallObjectMethod(g_originalClassLoadersMap, putMethod, threadGlobalRef, loaderGlobalRef);
        
        env->DeleteLocalRef(threadObj);
        if (originalLoader) env->DeleteLocalRef(originalLoader);
    }
    fprintf(log, "Saved %d original loaders.\n", threadCountForSave);
    fflush(log);

    // Подготавливаем reflection для defineClass
    jclass classClass = env->FindClass("java/lang/Class");
    if (env->ExceptionCheck() || !classClass) {
        fprintf(log, "Exception: FindClass java/lang/Class\n");
        fflush(log);
        env->ExceptionDescribe();
        env->ExceptionClear();
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Got Class class\n");
    fflush(log);

    jmethodID getDeclaredMethod = env->GetMethodID(classClass, "getDeclaredMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    if (env->ExceptionCheck()) {
        fprintf(log, "Exception: GetMethodID getDeclaredMethod\n");
        fflush(log);
        env->ExceptionDescribe();
        env->ExceptionClear();
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Got getDeclaredMethod\n");
    fflush(log);

    jclass stringClass = env->FindClass("java/lang/String");
    jclass byteArrayClass = env->FindClass("[B");
    jclass integerClass = env->FindClass("java/lang/Integer");
    jfieldID integerType = env->GetStaticFieldID(integerClass, "TYPE", "Ljava/lang/Class;");
    jobject intType = env->GetStaticObjectField(integerClass, integerType);

    jobjectArray paramTypes = env->NewObjectArray(4, classClass, NULL);
    env->SetObjectArrayElement(paramTypes, 0, stringClass);
    env->SetObjectArrayElement(paramTypes, 1, byteArrayClass);
    env->SetObjectArrayElement(paramTypes, 2, intType);
    env->SetObjectArrayElement(paramTypes, 3, intType);

    jstring defineClassName = env->NewStringUTF("defineClass");

    jobject defineClassMethodObj = env->CallObjectMethod(classLoaderClass, getDeclaredMethod, defineClassName, paramTypes);
    if (env->ExceptionCheck() || !defineClassMethodObj) {
        fprintf(log, "Exception: Call getDeclaredMethod for defineClass\n");
        fflush(log);
        env->ExceptionDescribe();
        env->ExceptionClear();
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Got defineClass Method object\n");
    fflush(log);

    jclass methodClass = env->FindClass("java/lang/reflect/Method");
    if (env->ExceptionCheck() || !methodClass) {
        fprintf(log, "Exception: FindClass java/lang/reflect/Method\n");
        fflush(log);
        env->ExceptionDescribe();
        env->ExceptionClear();
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    jmethodID setAccessibleMethod = env->GetMethodID(methodClass, "setAccessible", "(Z)V");
    env->CallVoidMethod(defineClassMethodObj, setAccessibleMethod, JNI_TRUE);
    if (env->ExceptionCheck()) {
        fprintf(log, "Exception: setAccessible\n");
        fflush(log);
        env->ExceptionDescribe();
        env->ExceptionClear();
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Set defineClass accessible\n");
    fflush(log);

    jmethodID invokeMethod = env->GetMethodID(methodClass, "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
    if (env->ExceptionCheck()) {
        fprintf(log, "Exception: GetMethodID invoke\n");
        fflush(log);
        env->ExceptionDescribe();
        env->ExceptionClear();
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Got invoke method\n");
    fflush(log);

    // Загружаем классы по порядку
	unsigned char* classPtr = classes;
	for (jsize i = 0; i < classCount; ++i) {
		jsize size = classSizes[i];
		
		jbyteArray byteArray = env->NewByteArray(size);
		if (!byteArray) {
			fprintf(log, "Error: NewByteArray failed for class %d\n", i);
			fflush(log);
			return 1;
		}

		env->SetByteArrayRegion(byteArray, 0, size, (const jbyte*)classPtr);
		if (env->ExceptionCheck()) {
			fprintf(log, "Exception: SetByteArrayRegion for class %d\n", i);
			fflush(log);
			env->ExceptionDescribe(); env->ExceptionClear();
			return 1;
		}

		jobjectArray args = env->NewObjectArray(4, env->FindClass("java/lang/Object"), NULL);
		env->SetObjectArrayElement(args, 0, NULL);
		env->SetObjectArrayElement(args, 1, byteArray);
		jmethodID integerCtor = env->GetMethodID(integerClass, "<init>", "(I)V");
		jobject zeroObj = env->NewObject(integerClass, integerCtor, 0);
		jobject sizeObj = env->NewObject(integerClass, integerCtor, (jint)size);
		env->SetObjectArrayElement(args, 2, zeroObj);
		env->SetObjectArrayElement(args, 3, sizeObj);

		jobject definedClass = env->CallObjectMethod(defineClassMethodObj, invokeMethod, launchLoader, args);
		if (env->ExceptionCheck() || !definedClass) {
			fprintf(log, "Exception: defineClass for class %d\n", i);
			fflush(log);
			env->ExceptionDescribe();
			env->ExceptionClear();
			g_jvm->DetachCurrentThread();
			fclose(log);
			return 1;
		}
		fprintf(log, "Defined class %d\n", i);
		fflush(log);

		classPtr += size;
		
		env->DeleteLocalRef(byteArray);
		env->DeleteLocalRef(args);
		env->DeleteLocalRef(zeroObj);
		env->DeleteLocalRef(sizeObj);
		env->DeleteLocalRef(definedClass);
	}
    fprintf(log, "All classes defined\n");
    fflush(log);
    
    // =========================================================================
    // === НАЧАЛО НОВОЙ ЛОГИКИ: ЗАГРУЗКА РЕСУРСОВ В ПАМЯТЬ =====================
    // =========================================================================

    // 1. Создаем HashMap с ресурсами в JVM, используя нашу новую функцию
    jobject resourceMap = CreateResourceMapInJava(env, log);
    if (!resourceMap) {
        fprintf(log, "FATAL: Failed to create resource map in Java.\n");
        fflush(log);
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }

    // 2. Находим наш кастомный класс InMemoryResourceLoader
    //    (Он должен быть одним из классов, загруженных на предыдущем шаге)
    fprintf(log, "Attempting to load InMemoryResourceLoader class via LaunchClassLoader...\n");
    fflush(log);
    
    // Получаем метод loadClass(String)
    jmethodID loadClassMethod = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    if (!loadClassMethod) {
        fprintf(log, "FATAL: Could not get method ID for ClassLoader.loadClass.\n");
        fflush(log);
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }

    // ВАЖНО: для loadClass имя класса передается с точками, а не со слэшами
    jstring loaderClassName = env->NewStringUTF("ru.fuctorial.fuctorize.InMemoryResourceLoader");
    
    jclass inMemoryLoaderClass = (jclass)env->CallObjectMethod(launchLoader, loadClassMethod, loaderClassName);
    env->DeleteLocalRef(loaderClassName); // Очищаем строку сразу после использования

    if (!inMemoryLoaderClass || env->ExceptionCheck()) {
        fprintf(log, "FATAL: Could not load InMemoryResourceLoader class via LaunchClassLoader. Make sure it's included in 'classes.h'.\n");
        fflush(log);
        if(env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Found InMemoryResourceLoader class\n");
    fflush(log);

    // 3. Находим его конструктор, который принимает Map и ClassLoader
    jmethodID loaderCtor = env->GetMethodID(inMemoryLoaderClass, "<init>", "(Ljava/util/Map;Ljava/lang/ClassLoader;)V");
    if (!loaderCtor || env->ExceptionCheck()) {
        fprintf(log, "FATAL: Could not find InMemoryResourceLoader constructor (Map, ClassLoader).\n");
        fflush(log);
        if(env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }

    // 4. Создаем экземпляр нашего ClassLoader'а, передавая ему карту ресурсов и родительский loader
    jobject customLoader = env->NewObject(inMemoryLoaderClass, loaderCtor, resourceMap, launchLoader);
    if (!customLoader || env->ExceptionCheck()) {
        fprintf(log, "FATAL: Could not instantiate InMemoryResourceLoader.\n");
        fflush(log);
        if(env->ExceptionCheck()) { env->ExceptionDescribe(); env->ExceptionClear(); }
        g_jvm->DetachCurrentThread();
        fclose(log);
        return 1;
    }
    fprintf(log, "Instantiated InMemoryResourceLoader\n");
    fflush(log);

    // 5. Устанавливаем наш кастомный loader как ContextClassLoader для текущего потока.
    //    Это стандартный способ подменить загрузчик ресурсов для многих фреймворков.
// === Устанавливаем customLoader как ContextClassLoader для ВСЕХ найденных Java-потоков ===
jmethodID setContextClassLoaderMethod = env->GetMethodID(threadClass, "setContextClassLoader", "(Ljava/lang/ClassLoader;)V");
if (!setContextClassLoaderMethod) {
    fprintf(log, "WARN: Could not find Thread.setContextClassLoader method.\n");
    fflush(log);
} else {
    jsize threadCount = env->GetArrayLength(threads);
    fprintf(log, "Setting context class loader for %d threads...\n", threadCount);
    fflush(log);

    for (jsize ti = 0; ti < threadCount; ++ti) {
        jobject threadObj = env->GetObjectArrayElement(threads, ti);
        if (!threadObj) {
            fprintf(log, "WARN: thread[%d] is NULL\n", ti);
            fflush(log);
            continue;
        }

        // Попытка установить loader — может бросить исключение для некоторых системных/нулевых потоков
        env->CallVoidMethod(threadObj, setContextClassLoaderMethod, customLoader);
        if (env->ExceptionCheck()) {
            fprintf(log, "WARN: Exception when setting contextClassLoader for thread %d\n", ti);
            env->ExceptionDescribe();
            env->ExceptionClear();
        } else {
            fprintf(log, "Set contextClassLoader for thread %d\n", ti);
        }
        fflush(log);

        env->DeleteLocalRef(threadObj);
    }
}


    fprintf(log, "Successfully set custom loader as ContextClassLoader.\n");
    fflush(log);

    // =========================================================================
    // === КОНЕЦ НОВОЙ ЛОГИКИ ==================================================
    // =========================================================================

    // Загружаем JavaInjector
    jstring injectorClassName = env->NewStringUTF("ru.fuctorial.fuctorize.JavaInjector");
     loadClassMethod = env->GetMethodID(classLoaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;");
    // ВАЖНО: Используем наш кастомный загрузчик для загрузки главного класса,
    // чтобы он и все последующие классы использовали его для поиска ресурсов.
    jclass injectorClass = (jclass)env->CallObjectMethod(customLoader, loadClassMethod, injectorClassName);
    if (env->ExceptionCheck() || !injectorClass) { 
        fprintf(log, "Exception: loadClass JavaInjector\n");
        fflush(log);
        env->ExceptionDescribe(); 
        env->ExceptionClear(); 
        g_jvm->DetachCurrentThread(); 
        fclose(log);
        return 1; 
    }
    fprintf(log, "Loaded JavaInjector class\n");
    fflush(log);

    jmethodID ctor = env->GetMethodID(injectorClass, "<init>", "()V");
    jobject localInjectorInstance = env->NewObject(injectorClass, ctor); // Create local instance first

    // MODIFIED: Check for exception and store instance as a global reference
    if (env->ExceptionCheck() || !localInjectorInstance) { 
        fprintf(log, "Exception: NewObject JavaInjector\n");
        fflush(log);
        env->ExceptionDescribe(); 
        env->ExceptionClear(); 
        g_jvm->DetachCurrentThread(); 
        fclose(log);
        // CleanupAndExit(); // Call cleanup on failure
        return 1; 
    }
    fprintf(log, "Instantiated JavaInjector\n");
    fflush(log);
    g_javaInjectorInstance = env->NewGlobalRef(localInjectorInstance);
    env->DeleteLocalRef(localInjectorInstance);

    fprintf(log, "Instantiated JavaInjector and stored global reference.\n"); fflush(log);

    // --- НОВЫЙ БЛОК: КЭШИРОВАНИЕ КЛАССОВ ДЛЯ UNLOAD ---
    fprintf(log, "Caching classes for unload...\n");
    fflush(log);
    
    jclass local_unloadCallbackClass = env->FindClass("ru/fuctorial/fuctorize/utils/UnloadCallback");
    if(local_unloadCallbackClass) g_unloadCallbackClass = (jclass)env->NewGlobalRef(local_unloadCallbackClass);
    
    jclass local_javaInjectorClass = env->FindClass("ru/fuctorial/fuctorize/JavaInjector");
    if(local_javaInjectorClass) g_javaInjectorClass = (jclass)env->NewGlobalRef(local_javaInjectorClass);

    jclass local_threadClass = env->FindClass("java/lang/Thread");
    if(local_threadClass) g_threadClass = (jclass)env->NewGlobalRef(local_threadClass);

    jclass local_mapClass = env->FindClass("java/util/Map");
    if(local_mapClass) g_mapClass = (jclass)env->NewGlobalRef(local_mapClass);
    
    jclass local_setClass = env->FindClass("java/util/Set");
    if(local_setClass) g_setClass = (jclass)env->NewGlobalRef(local_setClass);

    jclass local_iteratorClass = env->FindClass("java/util/Iterator");
    if(local_iteratorClass) g_iteratorClass = (jclass)env->NewGlobalRef(local_iteratorClass);

    jclass local_entryClass = env->FindClass("java/util/Map$Entry");
    if(local_entryClass) g_entryClass = (jclass)env->NewGlobalRef(local_entryClass);

    // Удаляем локальные ссылки
    if(local_unloadCallbackClass) env->DeleteLocalRef(local_unloadCallbackClass);
    if(local_javaInjectorClass) env->DeleteLocalRef(local_javaInjectorClass);
    if(local_threadClass) env->DeleteLocalRef(local_threadClass);
    if(local_mapClass) env->DeleteLocalRef(local_mapClass);
    if(local_setClass) env->DeleteLocalRef(local_setClass);
    if(local_iteratorClass) env->DeleteLocalRef(local_iteratorClass);
    if(local_entryClass) env->DeleteLocalRef(local_entryClass);

    fprintf(log, "Finished caching classes.\n");
    fflush(log);
    // --- КОНЕЦ НОВОГО БЛОКА ---

    g_jvm->DetachCurrentThread();
    fprintf(log, "Success: Detached thread\n");
    fflush(log);
    fclose(log);
    return 0; 
}



// Final cleanup and self-destruct function
void CleanupAndExit() {
    // This function ensures that we exit the thread and free the library,
    // allowing the injector to safely release the memory.
    FreeLibraryAndExitThread(g_hModule, 0);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        // FIXED: Store our module handle in the global variable
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(NULL, 0, InjectThread, hModule, 0, NULL);
    }
    return TRUE;
}


jobject CreateResourceMapInJava(JNIEnv* env, FILE* log) {
    fprintf(log, "Starting in-memory resource loading...\n");
    fflush(log);

    if (resourceCount == 0) {
        fprintf(log, "No resources to load. Skipping.\n");
        fflush(log);
    }

    jclass mapClass = env->FindClass("java/util/HashMap");
    if (!mapClass) {
        fprintf(log, "FATAL: FindClass java/util/HashMap failed.\n");
        fflush(log);
        return NULL;
    }
    jmethodID mapCtor = env->GetMethodID(mapClass, "<init>", "()V");
    if (!mapCtor) {
        fprintf(log, "FATAL: GetMethodID for HashMap constructor failed.\n");
        fflush(log);
        return NULL;
    }
    jobject resourceMap = env->NewObject(mapClass, mapCtor);

    jmethodID putMethod = env->GetMethodID(mapClass, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    if (!putMethod) {
        fprintf(log, "FATAL: GetMethodID for HashMap.put failed.\n");
        fflush(log);
        return NULL;
    }

    fprintf(log, "ResourceCount = %d\n", resourceCount);
    fflush(log);

    for (jsize i = 0; i < resourceCount; ++i) {
        const char* path = resourcePaths[i] ? resourcePaths[i] : "(null)";
        int size = resourceSizes[i];
        int offset = resourceOffsets[i];

        fprintf(log, "[RES] #%d path='%s' size=%d offset=%d\n", i, path, size, offset);
        fflush(log);

        if (resourcePaths[i] == NULL) {
            fprintf(log, "[RES] #%d SKIP: path is NULL\n", i);
            fflush(log);
            continue;
        }

        jstring key = env->NewStringUTF(resourcePaths[i]);
        if (!key) {
            fprintf(log, "[RES] #%d ERROR: NewStringUTF failed for '%s'\n", i, resourcePaths[i]);
            fflush(log);
            continue;
        }

        jbyteArray value = env->NewByteArray(size);
        if (!value) {
            fprintf(log, "[RES] #%d ERROR: NewByteArray failed for '%s' size=%d\n", i, resourcePaths[i], size);
            fflush(log);
            env->DeleteLocalRef(key);
            continue;
        }

        env->SetByteArrayRegion(value, 0, size, (const jbyte*)(resources_data + offset));
        if (env->ExceptionCheck()) {
            fprintf(log, "[RES] #%d EXC: Exception after SetByteArrayRegion for '%s'\n", i, resourcePaths[i]);
            env->ExceptionDescribe();
            env->ExceptionClear();
            fflush(log);
            env->DeleteLocalRef(key);
            env->DeleteLocalRef(value);
            continue;
        }

        jobject removed = env->CallObjectMethod(resourceMap, putMethod, key, value);
        if (env->ExceptionCheck()) {
            fprintf(log, "[RES] #%d EXC: Exception during map.put for '%s'\n", i, resourcePaths[i]);
            env->ExceptionDescribe();
            env->ExceptionClear();
            fflush(log);
            env->DeleteLocalRef(key);
            env->DeleteLocalRef(value);
            continue;
        }

        if (removed) {
            env->DeleteLocalRef(removed);
        }

        env->DeleteLocalRef(key);
        env->DeleteLocalRef(value);
    }

    fprintf(log, "In-memory resource map with %d entries created successfully.\n", resourceCount);
    fflush(log);
    
    env->DeleteLocalRef(mapClass);
    return resourceMap;
}

