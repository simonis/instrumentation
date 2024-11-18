#include <jvmti.h>
#include <stdio.h>
#include <string.h>

#ifndef _GNU_SOURCE
#  error "need _GNU_SOURCE for memmem()"
#endif

const char* classPattern = "io/simonis/InstrumentationTest";

// The class `InstrumentationTest` must contain a string constant of the form:
// "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx0"
//  1234567890123456789012345678901234567890123456789
//
const char* stringPattern = "xxxxxxxx";

// 'stringPattern' must be included 6 times at the beginning of the string followed by the '0' character
const int patternCount = 6;

// The watermark we write into the string if we transform the class
const char* watermark = "JVMTI";

static void printClass(jvmtiEnv* jvmti, jclass klass, const char* prefix) {
  char *className;
  if (jvmti->GetClassSignature(klass, &className, NULL) != JVMTI_ERROR_NONE) {
    fprintf(stderr, "%s%s of %p\n", prefix, "Can't get class signature", klass);
    return;
  }
  ++className; // Ignore leading 'L'
  if (strstr(className, classPattern) == className) {
    className[strlen(className) - 1] = '\0'; // Strip trailing ';'
    fprintf(stdout, "%s%s\n", prefix, className);
    fflush (NULL);
  }
  jvmti->Deallocate((unsigned char*) --className);
}

static char getID(jvmtiEnv *jvmti) {
  // The id was saved direcdtly in the JVMTI Environment Local Storage pointer (see 'Agent_OnLoad()')
  void* dummy;
  jvmti->GetEnvironmentLocalStorage(&dummy);
  return (char)(long)(dummy);
}

void JNICALL vmInit(jvmtiEnv *jvmti, JNIEnv* jni, jthread thread) {
  fprintf(stdout, "JVMTI - VMInit %c\n", getID(jvmti));
}

void JNICALL classLoadCallback(jvmtiEnv* jvmti, JNIEnv* jni, jthread thread, jclass klass) {
  printClass(jvmti, klass, "JVMTI - ClassLoad:    ");
}

void JNICALL classPrepareCallback(jvmtiEnv* jvmti, JNIEnv* jni, jthread thread, jclass klass) {
  printClass(jvmti, klass, "JVMTI - ClassPrepare: ");
}

void JNICALL classFileLoadCallback(jvmtiEnv* jvmti, JNIEnv* jni,
                                   jclass class_being_redefined,
                                   jobject loader,
                                   const char* name,
                                   jobject protection_domain,
                                   jint class_data_len,
                                   const unsigned char* class_data,
                                   jint* new_class_data_len,
                                   unsigned char** new_class_data) {

  if (strstr(name, classPattern) == name) {
    fprintf(stdout, "JVMTI - FileLoad:     %s (%p)\n", name, loader);
    if (class_being_redefined == NULL) {
      if (jvmti->Allocate(class_data_len, new_class_data) == JVMTI_ERROR_NONE) {
        memcpy(*new_class_data, class_data, class_data_len);
        *new_class_data_len = class_data_len;
        char* location = (char*)memmem(*new_class_data, class_data_len, stringPattern, strlen(stringPattern));
        if (location != NULL) {
          // poor man's redefinition
          char version = location[patternCount * strlen(stringPattern)];
          // Increment version for every transformation
          location[patternCount * strlen(stringPattern)] = version + 1;
          // Write the old version, watermark and id at the index given by the version (i.e. the oder of transformation)
          int newLocation = ((version - '0' + 1) * strlen(stringPattern));
          location[newLocation] = version;
          memcpy(location + newLocation + 1, watermark, strlen(watermark));
          location[newLocation + 1 + strlen(watermark)] = getID(jvmti);
        }
      } else {
        fprintf(stderr, "jvmti->Allocate(%d) failed\n", class_data_len);
        return;
      }
    }
  }
}

extern "C"
JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM* jvm, char* options, void* reserved) {
  jvmtiEnv* jvmti = NULL;
  jvmtiCapabilities capa;
  jvmtiError error;

  char id = 'a';
  if (options) {
    id = options[0];
  }

  jint result = jvm->GetEnv((void**) &jvmti, JVMTI_VERSION_1_1);
  if (result != JNI_OK) {
    fprintf(stderr, "Can't access JVMTI!\n");
    return JNI_ERR;
  }
  // Save agent id in JVMTI Environment Local Storage
  // (this is usually a pointer to allocated memory, but for us a char is enough).
  jvmti->SetEnvironmentLocalStorage(reinterpret_cast<void*>(id));

  jvmtiEventCallbacks callbacks;
  memset(&callbacks, 0, sizeof(jvmtiEventCallbacks));
  callbacks.VMInit = vmInit;
  callbacks.ClassLoad = classLoadCallback;
  callbacks.ClassPrepare = classPrepareCallback;
  callbacks.ClassFileLoadHook = classFileLoadCallback;

  if (jvmti->SetEventCallbacks(&callbacks, sizeof(jvmtiEventCallbacks)) != JVMTI_ERROR_NONE) {
    fprintf(stderr, "Can't set event callbacks!\n");
    return JNI_ERR;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_VM_INIT, NULL) != JVMTI_ERROR_NONE) {
    fprintf(stderr, "Can't enable JVMTI_EVENT_VM_INIT!\n");
    return JNI_ERR;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_LOAD, NULL) != JVMTI_ERROR_NONE) {
    fprintf(stderr, "Can't enable JVMTI_EVENT_CLASS_LOAD!\n");
    return JNI_ERR;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_PREPARE, NULL) != JVMTI_ERROR_NONE) {
    fprintf(stderr, "Can't enable JVMTI_EVENT_CLASS_PREPARE!\n");
    return JNI_ERR;
  }
  if (jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL) != JVMTI_ERROR_NONE) {
    fprintf(stderr, "Can't enable JVMTI_EVENT_CLASS_FILE_LOAD_HOOK!\n");
    return JNI_ERR;
  }
  fprintf(stdout, "JVMTI - agent  %c for  %s installed\n", id, classPattern);
  return JNI_OK;
}
