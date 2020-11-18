package com.heaven7.android.libeventapp;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;

import android.Manifest;
import android.os.Bundle;
import android.view.View;

import com.heaven7.core.util.PermissionHelper;

public class MainActivity extends AppCompatActivity {

    private final PermissionHelper mHelper = new PermissionHelper(this);

    static {
        System.loadLibrary("jnitest");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        mHelper.startRequestPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE, 1, new PermissionHelper.ICallback() {
            @Override
            public void onRequestPermissionResult(String s, int i, boolean b) {
                System.out.println("WRITE_EXTERNAL_STORAGE = " + b);
            }
            @Override
            public boolean handlePermissionHadRefused(String s, int i, Runnable runnable) {
                return false;
            }
        });
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, @NonNull String[] permissions, @NonNull int[] grantResults) {
        mHelper.onRequestPermissionsResult(requestCode, permissions, grantResults);
    }

    public static native void nTest0();

    public void onClickEventStart(View view) {
        new Thread(new Runnable() {
            @Override
            public void run() {
                nTest0();
            }
        }).start();
    }
    public void onClickEventEnd(View view) {

    }
}
