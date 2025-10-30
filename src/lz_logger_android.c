#include "lz_logger.h"
#include <jni.h>

JNIEXPORT void JNICALL Java_com_example_lz_1logger_LzLogger_nativeLog(
    JNIEnv *env, jclass clazz, jint loglevel, jstring tag, jstring file,
    jint line, jstring message) {
  (void)clazz;
  const char *tag_chars = (*env)->GetStringUTFChars(env, tag, NULL);
  const char *file_chars = (*env)->GetStringUTFChars(env, file, NULL);
  const char *message_chars = (*env)->GetStringUTFChars(env, message, NULL);

  lz_logger(loglevel, tag_chars, file_chars, line, message_chars);

  (*env)->ReleaseStringUTFChars(env, tag, tag_chars);
  (*env)->ReleaseStringUTFChars(env, file, file_chars);
  (*env)->ReleaseStringUTFChars(env, message, message_chars);
}
