/*
 *	JNativeHook - GrabKeyHook - 09/08/06
 *  Alexander Barker
 *
 *	JNI Interface for setting a Keyboard Hook and monitoring
 *	it with java.
 *
 *  TODO Add LGPL License
 */

/*
Compiling Options:
	gcc -m32 -march=i586 -shared -fPIC -lX11 -I${JAVA_HOME}/include -I${JAVA_HOME}/include/linux ./org_jnativehook_keyboard_GrabKeyHook.c -o libJNativeHook_Keyboard.so
	gcc -m64 -march=k8 -shared -fPIC -lX11 -I${JAVA_HOME}/include -I${JAVA_HOME}/include/linux ./org_jnativehook_keyboard_GrabKeyHook.c -o libJNativeHook_Keyboard.so
*/

#ifdef UNUSED
#elif defined(__GNUC__)
# define UNUSED(x) UNUSED_ ## x __attribute__((unused))
#elif defined(__LCLINT__)
# define UNUSED(x) /*@unused@*/ x
#else
# define UNUSED(x) x
#endif


#ifdef DEBUG
#include <stdio.h>
#include <unistd.h>
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

#include <w32api.h>
#define WINVER Windows2000
#define _WIN32_WINNT WINVER
#include <windows.h>

#include <jni.h>

#include "include/org_jnativehook_GlobalScreen.h"
#include "include/JConvertToNative.h"
#include "WinKeyCodes.h"

//Instance Variables
bool bRunning = TRUE;
HHOOK handleKeyboardHook = NULL;
HHOOK handleMouseHook = NULL;
HINSTANCE hInst = NULL;

unsigned short int keysize = 0;
KeyCode * grabkeys = NULL;

unsigned short int buttonsize = 0;
ButtonCode * grabbuttons = NULL;


JavaVM * jvm = NULL;
//pthread_t hookThreadId = 0;

void jniFatalError(char * message) {
	//Attach to the currently running jvm
	JNIEnv * env = NULL;
	(*jvm)->AttachCurrentThread(jvm, (void **)(&env), NULL);

	#ifdef DEBUG
	printf("Native: Fatal Error - %s\n", message);
	#endif

	(*env)->FatalError(env, message);
	exit(1);
}

void throwException(char * message) {
	//Attach to the currently running jvm
	JNIEnv * env = NULL;
	(*jvm)->AttachCurrentThread(jvm, (void **)(&env), NULL);

	//Locate our exception class
	jclass objExceptionClass = (*env)->FindClass(env, "org/jnativehook/keyboard/NativeKeyException");

	if (objExceptionClass != NULL) {
		#ifdef DEBUG
		printf("Native: Exception - %s\n", message);
		#endif

		(*env)->ThrowNew(env, objExceptionClass, message);
	}
	else {
		//Unable to find exception class, Terminate with error.
		jniFatalError("Unable to locate NativeKeyException class.");
	}
}

LRESULT CALLBACK LowLevelProc(int nCode, WPARAM wParam, LPARAM lParam) {
	//Attach to the currently running jvm
	JNIEnv * env = NULL;
	if ((*jvm)->AttachCurrentThread(jvm, (void **)(&env), NULL) >= 0) {

		//Class and Constructor for the NativeKeyEvent Object
		jclass clsKeyEvent = (*env)->FindClass(env, "org/jnativehook/keyboard/NativeKeyEvent");
		jmethodID KeyEvent_ID = (*env)->GetMethodID(env, clsKeyEvent, "<init>", "(IJIICI)V");

		//Class and Constructor for the NativeMouseEvent Object
		jclass clsButtonEvent = (*env)->FindClass(env, "org/jnativehook/mouse/NativeMouseEvent");
		jmethodID MouseEvent_ID = (*env)->GetMethodID(env, clsButtonEvent, "<init>", "(IJIIIIZI)V");

		//Class and getInstance method id for the GlobalScreen Object
		jclass clsGlobalScreen = (*env)->FindClass(env, "org/jnativehook/GlobalScreen");
		jmethodID getInstance_ID = (*env)->GetStaticMethodID(env, clsGlobalScreen, "getInstance", "()Lorg/jnativehook/GlobalScreen;");

		//A reference to the GlobalScreen Object
		jobject objGlobalScreen = (*env)->CallStaticObjectMethod(env, clsGlobalScreen, getInstance_ID);

		//ID's for the pressed, typed and released callbacks
		jmethodID fireKeyPressed_ID = (*env)->GetMethodID(env, clsGlobalScreen, "fireKeyPressed", "(Lorg/jnativehook/keyboard/NativeKeyEvent;)V");
		jmethodID fireKeyReleased_ID = (*env)->GetMethodID(env, clsGlobalScreen, "fireKeyReleased", "(Lorg/jnativehook/keyboard/NativeKeyEvent;)V");

		jmethodID fireMousePressed_ID = (*env)->GetMethodID(env, clsGlobalScreen, "fireMousePressed", "(Lorg/jnativehook/mouse/NativeMouseEvent;)V");
		jmethodID fireMouseReleased_ID = (*env)->GetMethodID(env, clsGlobalScreen, "fireMousePressed", "(Lorg/jnativehook/mouse/NativeMouseEvent;)V");


		unsigned int modifiers = 0;
		KBDLLHOOKSTRUCT * p = (KBDLLHOOKSTRUCT *)lParam;
		JKeyCode jkey;
		jobject objEvent = NULL;

		switch (wParam) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				#ifdef DEBUG
				printf("Native: LowLevelProc - Key pressed (%i)\n", p.vkCode);
				#endif

				if (p->vkCode == VK_LSHIFT)		setModifierMask(MOD_LSHIFT);
				if (p->vkCode == VK_RSHIFT)		setModifierMask(MOD_RSHIFT);

				if (p->vkCode == VK_LCONTROL)	setModifierMask(MOD_LCONTROL);
				if (p->vkCode == VK_RCONTROL)	setModifierMask(MOD_RCONTROL);

				if (p->vkCode == VK_LMENU)		setModifierMask(MOD_LALT);
				if (p->vkCode == VK_RMENU)		setModifierMask(MOD_RALT);

				if (p->vkCode == VK_LWIN)		setModifierMask(MOD_LWIN);
				if (p->vkCode == VK_RWIN)		setModifierMask(MOD_RWIN);


				jkey = NativeToJKeycode(p->vkCode);
				if (isMaskSet(MOD_SHIFT))		modifiers |= NativeToJModifier(MOD_SHIFT);
				if (isMaskSet(MOD_CONTROL))		modifiers |= NativeToJModifier(MOD_CONTROL);
				if (isMaskSet(MOD_ALT))			modifiers |= NativeToJModifier(MOD_ALT);
				if (isMaskSet(MOD_WIN))			modifiers |= NativeToJModifier(MOD_WIN);

				objEvent = (*env)->NewObject(env, clsKeyEvent, KeyEvent_ID, JK_KEY_PRESSED, (jlong) p->time, modifiers, jkey.keycode, (jchar) jkey.keycode, jkey.location);
				(*env)->CallVoidMethod(env, objGlobalScreen, fireKeyPressed_ID, objEvent);
				objEvent = NULL;
			break;

			case WM_KEYUP:
			case WM_SYSKEYUP:
				#ifdef DEBUG
				printf("Native: LowLevelProc - Key released(%i)\n", p.vkCode);
				#endif

				if (p->vkCode == VK_LSHIFT)		unsetModifierMask(MOD_LSHIFT);
				if (p->vkCode == VK_RSHIFT)		unsetModifierMask(MOD_RSHIFT);

				if (p->vkCode == VK_LCONTROL)	unsetModifierMask(MOD_LCONTROL);
				if (p->vkCode == VK_RCONTROL)	unsetModifierMask(MOD_RCONTROL);

				if (p->vkCode == VK_LMENU)		unsetModifierMask(MOD_LALT);
				if (p->vkCode == VK_RMENU)		unsetModifierMask(MOD_RALT);

				if (p->vkCode == VK_LWIN)		unsetModifierMask(MOD_LWIN);
				if (p->vkCode == VK_RWIN)		unsetModifierMask(MOD_RWIN);


				jkey = NativeToJKeycode(p->vkCode);
				if (isMaskSet(MOD_SHIFT))		modifiers |= NativeToJModifier(MOD_SHIFT);
				if (isMaskSet(MOD_CONTROL))		modifiers |= NativeToJModifier(MOD_CONTROL);
				if (isMaskSet(MOD_ALT))			modifiers |= NativeToJModifier(MOD_ALT);
				if (isMaskSet(MOD_WIN))			modifiers |= NativeToJModifier(MOD_WIN);

				objEvent = (*env)->NewObject(env, clsKeyEvent, KeyEvent_ID, JK_KEY_RELEASED, (jlong) p->time, modifiers, jkey.keycode, (jchar) jkey.keycode, jkey.location);
				(*env)->CallVoidMethod(env, objGlobalScreen, fireKeyReleased_ID, objEvent);
				objEvent = NULL;
			break;


			case WM_LBUTTONDOWN:
			case WM_RBUTTONDOWN:
			case WM_MBUTTONDOWN:
			case WM_XBUTTONDOWN:
			case WM_NCXBUTTONDOWN:
				#ifdef DEBUG
				printf("Native: LowLevelProc - Button pressed (%i)\n", wParam);
				#endif
				//fwButton = GET_XBUTTON_WPARAM(wParam);

				jkey = NativeToJKeycode(p->vkCode);
				if (isMaskSet(MOD_SHIFT))		modifiers |= NativeToJModifier(MOD_SHIFT);
				if (isMaskSet(MOD_CONTROL))		modifiers |= NativeToJModifier(MOD_CONTROL);
				if (isMaskSet(MOD_ALT))			modifiers |= NativeToJModifier(MOD_ALT);
				if (isMaskSet(MOD_WIN))			modifiers |= NativeToJModifier(MOD_WIN);

				objEvent = (*env)->NewObject(env, clsKeyEvent, KeyEvent_ID, JK_KEY_PRESSED, (jlong) p->time, modifiers, jkey.keycode, (jchar) jkey.keycode, jkey.location);
				(*env)->CallVoidMethod(env, objGlobalScreen, fireKeyPressed_ID, objEvent);
				objEvent = NULL;
			break;


			case WM_LBUTTONUP:
			case WM_RBUTTONUP:
			case WM_MBUTTONUP:
			case WM_XBUTTONUP:
			case WM_NCXBUTTONUP:
				//fwButton = GET_XBUTTON_WPARAM(wParam);

				#ifdef DEBUG
				printf("Native: LowLevelProc - Button released(%i)\n", wParam);
				#endif

				jkey = NativeToJKeycode(p->vkCode);
				if (isMaskSet(MOD_SHIFT))		modifiers |= NativeToJModifier(MOD_SHIFT);
				if (isMaskSet(MOD_CONTROL))		modifiers |= NativeToJModifier(MOD_CONTROL);
				if (isMaskSet(MOD_ALT))			modifiers |= NativeToJModifier(MOD_ALT);
				if (isMaskSet(MOD_WIN))			modifiers |= NativeToJModifier(MOD_WIN);

				objEvent = (*env)->NewObject(env, clsKeyEvent, KeyEvent_ID, JK_KEY_RELEASED, (jlong) p->time, modifiers, jkey.keycode, (jchar) jkey.keycode, jkey.location);
				(*env)->CallVoidMethod(env, objGlobalScreen, fireKeyReleased_ID, objEvent);
				objEvent = NULL;
			break;

			case WM_MOUSEWHEEL:
			default:
			break;
		}
	}
	else {
		#ifdef DEBUG
		printf("Native: LowLevelProc - Error on the attach current thread.\n");
		#endif
	}

	return CallNextHookEx(NULL, nCode, wParam, lParam);
}

void MsgLoop() {
	MSG message;

	while (bRunning && GetMessage(&message, NULL, 0, 0)) {
		TranslateMessage(&message);
		DispatchMessage(&message);
	}

	#ifdef DEBUG
	printf("Native: MsgLoop() stop successful.\n");
	#endif
}

JNIEXPORT void JNICALL Java_org_jnativehook_GlobalScreen_grabKey(JNIEnv * UNUSED(env), jobject UNUSED(obj), jint jmodifiers, jint jkeycode, jint jkeylocation) {
	JKeyCode jkey;
	jkey.keycode = jkeycode;
	jkey.location = jkeylocation;

	#ifdef DEBUG
	printf("Native: grabKey - KeyCode(%i) Modifier(%X)\n", (unsigned int) jkeycode, jmodifiers);
	#endif

	KeyCode newkey;
	newkey.keycode = JKeycodeToNative(jkey);
	newkey.shift_mask = jmodifiers & JK_SHIFT_MASK;
	newkey.control_mask = jmodifiers & JK_CTRL_MASK;
	newkey.alt_mask = jmodifiers & JK_ALT_MASK;
	newkey.meta_mask = jmodifiers & JK_META_MASK;

	if (keysize == USHRT_MAX) {
		//TODO Throw exception
		return;
	}

	KeyCode * tmp = malloc( (keysize + 1) * sizeof(KeyCode) );

	int i = 0;
	for (; i < keysize; i++) {
		if (grabkeys[i].keycode			== newkey.keycode &&
			grabkeys[i].shift_mask		== newkey.shift_mask &&
			grabkeys[i].control_mask	== newkey.control_mask &&
			grabkeys[i].alt_mask		== newkey.alt_mask &&
			grabkeys[i].meta_mask		== newkey.meta_mask
		) {
			//We have a duplicate.
			free(tmp);
			return;
		}

		tmp[i] = grabkeys[i];
	}

	free(grabkeys);
	grabkeys = tmp;
	grabkeys[keysize] = newkey;
	keysize++;
}

JNIEXPORT void JNICALL Java_org_jnativehook_GlobalScreen_ungrabKey(JNIEnv * UNUSED(env), jobject UNUSED(obj), jint jmodifiers, jint jkeycode, jint jkeylocation) {
	JKeyCode jkey;
	jkey.keycode = jkeycode;
	jkey.location = jkeylocation;

	#ifdef DEBUG
	printf("Native: ungrabKey - KeyCode(%i) Modifier(%X)\n", (unsigned int) jkeycode, jmodifiers);
	#endif

	KeyCode newkey;
	newkey.keycode = JKeycodeToNative(jkey);
	newkey.shift_mask = jmodifiers & JK_SHIFT_MASK;
	newkey.control_mask = jmodifiers & JK_CTRL_MASK;
	newkey.alt_mask = jmodifiers & JK_ALT_MASK;
	newkey.meta_mask = jmodifiers & JK_META_MASK;

	KeyCode * tmp = malloc( (keysize - 1) * sizeof(KeyCode) );

	int i = 0, j = 0;
	for (; i < keysize; i++) {
		if (grabkeys[i].keycode			!= newkey.keycode ||
			grabkeys[i].shift_mask		!= newkey.shift_mask ||
			grabkeys[i].control_mask	!= newkey.control_mask ||
			grabkeys[i].alt_mask		!= newkey.alt_mask ||
			grabkeys[i].meta_mask		!= newkey.meta_mask
		) {
			tmp[j++] = grabkeys[i];
		}
	}

	free(grabkeys);
	keysize--;
	grabkeys = tmp;
}

JNIEXPORT void JNICALL Java_org_jnativehook_GlobalScreen_grabButton(JNIEnv * UNUSED(env), jobject UNUSED(obj), jint jbutton) {
	#ifdef DEBUG
	printf("Native: grabButton - Button(%i)\n", (unsigned int) button);
	#endif

	ButtonCode newbutton;
	newbutton.buttoncode = JButtonToNative(jbutton);
	/*
	newbutton.shift_mask = jmodifiers & JK_SHIFT_MASK;
	newbutton.control_mask = jmodifiers & JK_CTRL_MASK;
	newbutton.alt_mask = jmodifiers & JK_ALT_MASK;
	newbutton.meta_mask = jmodifiers & JK_META_MASK;
	*/

	if (buttonsize == USHRT_MAX) {
		//TODO Throw exception
		return;
	}

	ButtonCode * tmp = malloc( (buttonsize + 1) * sizeof(ButtonCode) );

	int i = 0;
	for (; i < buttonsize; i++) {
		if (grabbuttons[i].buttoncode		== newbutton.buttoncode &&
			grabbuttons[i].shift_mask		== newbutton.shift_mask &&
			grabbuttons[i].control_mask		== newbutton.control_mask &&
			grabbuttons[i].alt_mask			== newbutton.alt_mask &&
			grabbuttons[i].meta_mask		== newbutton.meta_mask
		) {
			//We have a duplicate.
			free(tmp);
			return;
		}

		tmp[i] = grabbuttons[i];
	}

	free(grabbuttons);
	grabbuttons = tmp;
	grabbuttons[buttonsize] = newbutton;
	buttonsize++;
}

JNIEXPORT void JNICALL Java_org_jnativehook_GlobalScreen_ungrabButton(JNIEnv * UNUSED(env), jobject UNUSED(obj), jint jbutton) {
	#ifdef DEBUG
	printf("Native: ungrabButton - Button(%i)\n", (unsigned int) button);
	#endif

	ButtonCode newbutton;
	newbutton.buttoncode = JButtonToNative(jbutton);
	/*
	newbutton.shift_mask = jmodifiers & JK_SHIFT_MASK;
	newbutton.control_mask = jmodifiers & JK_CTRL_MASK;
	newbutton.alt_mask = jmodifiers & JK_ALT_MASK;
	newbutton.meta_mask = jmodifiers & JK_META_MASK;
	*/

	ButtonCode * tmp = malloc( (buttonsize - 1) * sizeof(ButtonCode) );

	int i = 0, j = 0;
	for (; i < buttonsize; i++) {
		if (grabbuttons[i].buttoncode		!= newbutton.buttoncode ||
			grabbuttons[i].shift_mask		!= newbutton.shift_mask ||
			grabbuttons[i].control_mask		!= newbutton.control_mask ||
			grabbuttons[i].alt_mask			!= newbutton.alt_mask ||
			grabbuttons[i].meta_mask		!= newbutton.meta_mask
		) {
			tmp[j++] = grabbuttons[i];
		}
	}

	free(grabbuttons);
	grabbuttons = tmp;
	buttonsize--;
}


//This is where java attaches to the native machine.  Its kind of like the java + native constructor.
JNIEXPORT void JNICALL Java_org_jnativehook_GlobalScreen_initialize(JNIEnv * env, jobject UNUSED(obj)) {
	//Grab the currently running virtual machine so we can attach to it in
	//functions that are not called from java. ( I.E. MsgLoop )
	(*env)->GetJavaVM(env, &jvm);

	//Setup the native hooks and their callbacks.
	handleKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelProc, hInst, 0);

	if (handleKeyboardHook != NULL) {
		#ifdef DEBUG
		printf("Native: SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelProc, hInst, 0) successful\n");
		#endif
	}
	else {
		#ifdef DEBUG
		printf("Native: SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelProc, hInst, 0) failed\n");
		#endif

		throwException("Failed to hook keyboard using SetWindowsHookEx");

		//Naturaly exit so jni exception is thrown.
		return;
	}


	handleMouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelProc, hInst, 0);
	if (handleMouseHook != NULL) {
		#ifdef DEBUG
		printf("Native: SetWindowsHookEx(WH_MOUSE_LL, LowLevelProc, hInst, 0) successful\n");
		#endif
	}
	else {
		#ifdef DEBUG
		printf("Native: SetWindowsHookEx(WH_MOUSE_LL, LowLevelProc, hInst, 0) failed\n");
		#endif

		throwException("Failed to hook mouse using SetWindowsHookEx");

		//Naturaly exit so jni exception is thrown.
		return;
	}

	bRunning = TRUE;

	/*
	if( pthread_create( &hookThreadId, NULL, (void *) &MsgLoop, NULL) ) {
		#ifdef DEBUG
		printf("Native: MsgLoop() start failure.\n");
		#endif
		//TODO Throw an exception
	}
	else {
		#ifdef DEBUG
		printf("Native: MsgLoop() start successful.\n");
		#endif
	}
	*/
}

JNIEXPORT void JNICALL Java_org_jnativehook_GlobalScreen_deinitialize(JNIEnv * UNUSED(env), jobject UNUSED(obj)) {
	if (handleKeyboardHook != NULL) {
		UnhookWindowsHookEx(handleKeyboardHook);
		handleKeyboardHook = NULL;
	}

	//Try to exit the thread naturally.
	bRunning = FALSE;
	//pthread_join(hookThreadId, NULL);

	#ifdef DEBUG
	printf("Native: Thread terminated successful.\n");
	#endif
}

BOOL APIENTRY DllMain(HINSTANCE _hInst, DWORD reason, LPVOID reserved) {
	switch (reason) {
		case DLL_PROCESS_ATTACH:
			#ifdef DEBUG
			printf("Native: DllMain - DLL Process attach.\n");
			#endif

			hInst = _hInst; //GetModuleHandle(NULL)
		break;

		case DLL_PROCESS_DETACH:
			#ifdef DEBUG
			printf("Native: DllMain - DLL Process Detach.\n");
			#endif
		break;

		default:
		break;
	}

	return TRUE;
}