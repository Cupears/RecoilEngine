package org.cupears.recoil.smoke;

import android.os.Bundle;
import android.util.Log;
import android.view.WindowManager;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

public final class MatchSmokeActivity extends SDLActivity {
    private static final String TAG = "RecoilMatchSmoke";
    private Process match;

    @Override
    protected void onCreate(Bundle state) {
        File directory = new File(getFilesDir(), "skirmish");
        File script = new File(directory, "skirmish.txt");
        if (!script.isFile()) {
            super.onCreate(state);
            Log.i(TAG, "Place skirmish.txt and its game data in the private skirmish directory");
            return;
        }
        try (FileOutputStream position = new FileOutputStream(new File(directory, "commander.bin"));
             FileOutputStream order = new FileOutputStream(new File(directory, "move.bin"))) {
            // Reset both files before either process starts.
        } catch (IOException error) {
            super.onCreate(state);
            Log.e(TAG, "Could not prepare the private match files", error);
            return;
        }
        super.onCreate(state);

        File executable = new File(getApplicationInfo().nativeLibraryDir, "librecoil_skirmish.so");
        ProcessBuilder launch = new ProcessBuilder(executable.getAbsolutePath(),
                "--nocolor", "--isolation-dir=" + directory.getAbsolutePath(), script.getAbsolutePath());
        launch.directory(directory);
        launch.environment().put("SPRING_WRITEDIR", directory.getAbsolutePath());
        launch.environment().put("RECOIL_ANDROID_MATCH_SMOKE", "1");
        launch.environment().put("LD_LIBRARY_PATH", getApplicationInfo().nativeLibraryDir);
        launch.redirectErrorStream(true);
        launch.redirectOutput(new File(directory, "console.log"));
        try {
            match = launch.start();
            getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
            Log.i(TAG, "Started the local skirmish engine");
        } catch (IOException error) {
            Log.e(TAG, "Could not start the local skirmish engine", error);
        }
    }

    @Override
    protected void onDestroy() {
        if (match != null) {
            match.destroyForcibly();
        }
        super.onDestroy();
    }
}
