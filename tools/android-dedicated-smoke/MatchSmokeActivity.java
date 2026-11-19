package org.cupears.recoil.smoke;

import android.os.Bundle;
import android.util.Log;
import org.libsdl.app.SDLActivity;
import java.io.File;
import java.io.IOException;

public final class MatchSmokeActivity extends SDLActivity {
    private static final String TAG = "RecoilMatchSmoke";
    private Process match;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        File directory = new File(getFilesDir(), "skirmish");
        File script = new File(directory, "skirmish.txt");
        if (!script.isFile()) {
            Log.i(TAG, "Place skirmish.txt and its game data in the private skirmish directory");
            return;
        }

        File executable = new File(getApplicationInfo().nativeLibraryDir, "librecoil_skirmish.so");
        ProcessBuilder launch = new ProcessBuilder(executable.getAbsolutePath(),
                "--nocolor", "--isolation-dir=" + directory.getAbsolutePath(), script.getAbsolutePath());
        launch.directory(directory);
        launch.environment().put("SPRING_WRITEDIR", directory.getAbsolutePath());
        launch.environment().put("LD_LIBRARY_PATH", getApplicationInfo().nativeLibraryDir);
        launch.redirectErrorStream(true);
        launch.redirectOutput(new File(directory, "console.log"));
        try {
            match = launch.start();
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
