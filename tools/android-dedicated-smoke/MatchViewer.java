package org.cupears.recoilmatch;

import android.app.Activity;
import android.os.Bundle;
import android.graphics.*;
import android.view.*;
import java.io.*;
import java.net.*;
import java.util.Locale;
import java.util.concurrent.*;

public class MatchViewer extends Activity {
    private final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor();
    private MapView map;
    private boolean terrainLoaded;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        map = new MapView();
        setContentView(map);
        worker.scheduleWithFixedDelay(this::poll, 0, 180, TimeUnit.MILLISECONDS);
    }

    @Override public void onDestroy() {
        worker.shutdownNow();
        super.onDestroy();
    }

    private String request(String command) throws IOException {
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress("127.0.0.1", 8454), 400);
            socket.setSoTimeout(600);
            Writer output = new OutputStreamWriter(socket.getOutputStream());
            output.write(command + "\n");
            output.flush();
            return new BufferedReader(new InputStreamReader(socket.getInputStream())).readLine();
        }
    }

    private void poll() {
        try {
            if (!terrainLoaded) {
                String response = request("TERRAIN");
                if (response != null && response.startsWith("TERRAIN ")) {
                    String[] words = response.split(" ");
                    if (words.length != 628) return;
                    int[] heights = new int[625];
                    for (int i = 0; i < heights.length; ++i) heights[i] = Integer.parseInt(words[i + 3]);
                    float width = Float.parseFloat(words[1]), height = Float.parseFloat(words[2]);
                    terrainLoaded = true;
                    runOnUiThread(() -> { map.width = width; map.height = height; map.heights = heights; });
                }
            }
            String response = request("STATE");
            if (response == null || !response.startsWith("STATE ")) return;
            String[] words = response.split(" ");
            int frame = Integer.parseInt(words[1]);
            float x = Float.parseFloat(words[3]), z = Float.parseFloat(words[5]);
            runOnUiThread(() -> { map.frame = frame; map.x = x; map.z = z; map.invalidate(); });
        } catch (IOException | NumberFormatException ignored) {
            runOnUiThread(() -> { map.frame = -1; map.invalidate(); });
        }
    }

    private class MapView extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final RectF bounds = new RectF();
        private int[] heights;
        private float width = 1, height = 1, x, z;
        private int frame = -1;
        private boolean selected;
        private String orderStatus = "";
        MapView() { super(MatchViewer.this); }

        @Override protected void onDraw(Canvas canvas) {
            canvas.drawColor(Color.rgb(18, 22, 31));
            float size = Math.min(getWidth() - 56, getHeight() - 130);
            float left = (getWidth() - size) / 2, top = 68;
            bounds.set(left, top, left + size, top + size);
            if (heights != null) {
                float cell = size / 25;
                for (int row = 0; row < 25; ++row) {
                    for (int col = 0; col < 25; ++col) {
                        int elev = heights[row * 25 + col];
                        int shade = Math.max(40, Math.min(183, 92 + elev / 5));
                        paint.setColor(elev < 0 ? Color.rgb(31, 73, 99) :
                            Color.rgb(shade, shade * 9 / 10, shade * 7 / 10));
                        canvas.drawRect(left + col * cell, top + row * cell,
                            left + (col + 1) * cell + 0.5f, top + (row + 1) * cell + 0.5f, paint);
                    }
                }
            }
            paint.setTextSize(29);
            paint.setColor(Color.WHITE);
            canvas.drawText(frame < 0 ? "Waiting for local BAR match" : "Altair Crossing  •  Frame " + frame,
                28, 47, paint);
            if (frame >= 0) {
                paint.setColor(selected ? Color.WHITE : Color.rgb(255, 151, 45));
                canvas.drawCircle(left + size * x / width, top + size * z / height, 16, paint);
                paint.setColor(Color.WHITE);
                paint.setTextSize(24);
                canvas.drawText(orderStatus.isEmpty() ? (selected ? "Tap terrain to move" : "Tap your commander") : orderStatus,
                    28, top + size + 40, paint);
            }
        }

        @Override public boolean onTouchEvent(android.view.MotionEvent event) {
            if (event.getAction() != MotionEvent.ACTION_UP || frame < 0 || heights == null) return true;
            float px = event.getX(), py = event.getY();
            if (Math.hypot(px - bounds.left - bounds.width() * x / width,
                           py - bounds.top - bounds.height() * z / height) <= 30) {
                selected = true;
                orderStatus = "";
                invalidate();
            } else if (selected && bounds.contains(px, py)) {
                float tx = (px - bounds.left) / bounds.width() * width;
                float tz = (py - bounds.top) / bounds.height() * height;
                worker.execute(() -> {
                    String response;
                    try { response = request(String.format(Locale.US, "MOVE %.2f %.2f", tx, tz)); }
                    catch (IOException ignored) { response = null; }
                    boolean ok = "OK".equals(response);
                    runOnUiThread(() -> { orderStatus = ok ? "Move order accepted" : "Move order failed"; invalidate(); });
                });
            }
            return true;
        }
    }
}
