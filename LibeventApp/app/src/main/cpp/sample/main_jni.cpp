//
// Created by Administrator on 2020/11/18 0018.
//

#include <jni.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int
main0(int argc, const char *path);

JNIEXPORT void JNICALL Java_com_heaven7_android_libeventapp_MainActivity_nTest0(JNIEnv *env, jclass cla) {
main0(0, "/sdcard/event.fifo");
}

#ifdef __cplusplus
}
#endif