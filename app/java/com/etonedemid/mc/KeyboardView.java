package com.etonedemid.mc;

import android.content.Context;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;

// An invisible text editor for the on-screen keyboard to type into, since NativeActivity's own view isn't one (so
// ANativeActivity_showSoftInput does nothing on current Android). What's typed goes to the game as characters and keys
// through glue.c: any language, not only what maps to key codes. Visible-password input: the keyboard sends text as it's
// typed instead of holding words back as suggestions.
public class KeyboardView extends View {
    static native void nativeChar(int codePoint);   // a typed character
    static native void nativeKey(int glfwKey);      // pressed and released: Backspace (259), Enter (257)

    public KeyboardView(Context c) { super(c); setFocusable(true); setFocusableInTouchMode(true); }

    @Override public boolean onCheckIsTextEditor() { return true; }

    @Override public InputConnection onCreateInputConnection(EditorInfo out) {
        out.inputType = InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_VISIBLE_PASSWORD;
        out.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_DONE;
        return new BaseInputConnection(this, false) {
            @Override public boolean commitText(CharSequence text, int pos) {
                String s = text.toString();
                for (int i = 0; i < s.length(); ) {
                    int cp = s.codePointAt(i);
                    if (cp == '\n') nativeKey(257); else nativeChar(cp);
                    i += Character.charCount(cp);
                }
                return true;
            }
            @Override public boolean deleteSurroundingText(int before, int after) {
                for (int i = 0; i < Math.max(before, 1); i++) nativeKey(259);
                return true;
            }
            @Override public boolean sendKeyEvent(KeyEvent e) {
                if (e.getAction() != KeyEvent.ACTION_DOWN) return true;
                if (e.getKeyCode() == KeyEvent.KEYCODE_DEL) nativeKey(259);
                else if (e.getKeyCode() == KeyEvent.KEYCODE_ENTER) nativeKey(257);
                else if (e.getUnicodeChar() != 0) nativeChar(e.getUnicodeChar());
                return true;
            }
            @Override public boolean performEditorAction(int action) { nativeKey(257); return true; }
        };
    }
}
