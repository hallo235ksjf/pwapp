package com.raheel.player;

import android.Manifest;
import android.animation.ObjectAnimator;
import android.animation.ValueAnimator;
import android.content.ContentUris;
import android.content.pm.PackageManager;
import android.database.Cursor;
import android.media.MediaPlayer;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.provider.MediaStore;
import android.view.View;
import android.view.animation.LinearInterpolator;
import android.widget.ImageButton;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public class MainActivity extends AppCompatActivity {

    private static final int PERMISSION_REQUEST_CODE = 100;

    private final List<Track> playlist = new ArrayList<>();
    private TrackAdapter adapter;
    private MediaPlayer mediaPlayer;
    private int currentIndex = -1;
    private boolean isPlaying = false;

    private TextView titleText, artistText, currentTimeText, totalTimeText, emptyStateText;
    private SeekBar progressBar, volumeBar;
    private ImageButton playPauseButton, nextButton, prevButton;
    private View coverView, coverGlow;
    private RecyclerView recyclerView;

    private final Handler handler = new Handler(Looper.getMainLooper());
    private ValueAnimator pulseAnimator;

    private final Runnable progressUpdater = new Runnable() {
        @Override
        public void run() {
            if (mediaPlayer != null && isPlaying) {
                int pos = mediaPlayer.getCurrentPosition();
                progressBar.setProgress(pos);
                currentTimeText.setText(formatTime(pos));
                handler.postDelayed(this, 300);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.activity_main);

        titleText = findViewById(R.id.titleText);
        artistText = findViewById(R.id.artistText);
        currentTimeText = findViewById(R.id.currentTimeText);
        totalTimeText = findViewById(R.id.totalTimeText);
        progressBar = findViewById(R.id.progressBar);
        volumeBar = findViewById(R.id.volumeBar);
        playPauseButton = findViewById(R.id.playPauseButton);
        nextButton = findViewById(R.id.nextButton);
        prevButton = findViewById(R.id.prevButton);
        coverView = findViewById(R.id.coverView);
        coverGlow = findViewById(R.id.coverGlow);
        recyclerView = findViewById(R.id.recyclerView);
        emptyStateText = findViewById(R.id.emptyStateText);

        adapter = new TrackAdapter(playlist, this::onTrackSelected);
        recyclerView.setLayoutManager(new LinearLayoutManager(this));
        recyclerView.setAdapter(adapter);

        playPauseButton.setOnClickListener(v -> togglePlayPause());
        nextButton.setOnClickListener(v -> playNext());
        prevButton.setOnClickListener(v -> playPrevious());

        volumeBar.setMax(100);
        volumeBar.setProgress(80);
        volumeBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (mediaPlayer != null) {
                    float vol = progress / 100f;
                    mediaPlayer.setVolume(vol, vol);
                }
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override public void onStopTrackingTouch(SeekBar seekBar) {}
        });

        progressBar.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override
            public void onProgressChanged(SeekBar seekBar, int progress, boolean fromUser) {
                if (fromUser) currentTimeText.setText(formatTime(progress));
            }
            @Override public void onStartTrackingTouch(SeekBar seekBar) {}
            @Override
            public void onStopTrackingTouch(SeekBar seekBar) {
                if (mediaPlayer != null) mediaPlayer.seekTo(seekBar.getProgress());
            }
        });

        checkPermissionAndLoadLibrary();
    }

    private void checkPermissionAndLoadLibrary() {
        String permission = Build.VERSION.SDK_INT >= 33
                ? Manifest.permission.READ_MEDIA_AUDIO
                : Manifest.permission.READ_EXTERNAL_STORAGE;

        if (ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED) {
            loadLibrary();
        } else {
            ActivityCompat.requestPermissions(this, new String[]{permission}, PERMISSION_REQUEST_CODE);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == PERMISSION_REQUEST_CODE) {
            if (grantResults.length > 0 && grantResults[0] == PackageManager.PERMISSION_GRANTED) {
                loadLibrary();
            } else {
                Toast.makeText(this, "Ohne Speicherzugriff kann ich keine Musik finden", Toast.LENGTH_LONG).show();
                emptyStateText.setVisibility(View.VISIBLE);
                emptyStateText.setText("Kein Zugriff auf die Musikbibliothek.\nErlaube den Zugriff in den App-Einstellungen.");
            }
        }
    }

    private void loadLibrary() {
        playlist.clear();
        Uri collection = MediaStore.Audio.Media.EXTERNAL_CONTENT_URI;
        String[] projection = {
                MediaStore.Audio.Media._ID,
                MediaStore.Audio.Media.TITLE,
                MediaStore.Audio.Media.ARTIST,
                MediaStore.Audio.Media.DURATION,
                MediaStore.Audio.Media.IS_MUSIC
        };
        String selection = MediaStore.Audio.Media.IS_MUSIC + " != 0";

        try (Cursor cursor = getContentResolver().query(collection, projection, selection, null,
                MediaStore.Audio.Media.TITLE + " ASC")) {
            if (cursor != null) {
                int idCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media._ID);
                int titleCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.TITLE);
                int artistCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.ARTIST);
                int durCol = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media.DURATION);

                while (cursor.moveToNext()) {
                    playlist.add(new Track(
                            cursor.getLong(idCol),
                            cursor.getString(titleCol),
                            cursor.getString(artistCol),
                            cursor.getLong(durCol)
                    ));
                }
            }
        } catch (SecurityException e) {
            Toast.makeText(this, "Zugriff verweigert", Toast.LENGTH_SHORT).show();
        }

        adapter.notifyDataSetChanged();

        if (playlist.isEmpty()) {
            emptyStateText.setVisibility(View.VISIBLE);
            emptyStateText.setText("Keine MP3s gefunden. Leg Musik auf dein Gerät\nund tippe erneut auf die App.");
        } else {
            emptyStateText.setVisibility(View.GONE);
            playTrackAt(0);
        }
    }

    private void onTrackSelected(int position) {
        playTrackAt(position);
    }

    private void playTrackAt(int index) {
        if (index < 0 || index >= playlist.size()) return;
        currentIndex = index;
        Track track = playlist.get(index);

        releasePlayer();

        Uri trackUri = ContentUris.withAppendedId(
                MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, track.id);

        mediaPlayer = MediaPlayer.create(this, trackUri);
        if (mediaPlayer == null) {
            Toast.makeText(this, "Konnte Titel nicht laden", Toast.LENGTH_SHORT).show();
            return;
        }

        float vol = volumeBar.getProgress() / 100f;
        mediaPlayer.setVolume(vol, vol);
        mediaPlayer.setOnCompletionListener(mp -> playNext());

        int duration = mediaPlayer.getDuration();
        progressBar.setMax(duration);
        totalTimeText.setText(formatTime(duration));
        currentTimeText.setText(formatTime(0));

        titleText.setText(track.title);
        artistText.setText(track.artist);
        adapter.setPlayingPosition(index);

        mediaPlayer.start();
        isPlaying = true;
        updatePlayPauseIcon();
        startPulse();
        handler.post(progressUpdater);
    }

    private void togglePlayPause() {
        if (mediaPlayer == null) {
            if (!playlist.isEmpty()) playTrackAt(0);
            return;
        }
        if (isPlaying) {
            mediaPlayer.pause();
            isPlaying = false;
            stopPulse();
        } else {
            mediaPlayer.start();
            isPlaying = true;
            startPulse();
            handler.post(progressUpdater);
        }
        updatePlayPauseIcon();
    }

    private void playNext() {
        if (playlist.isEmpty()) return;
        int next = (currentIndex + 1) % playlist.size();
        playTrackAt(next);
    }

    private void playPrevious() {
        if (playlist.isEmpty()) return;
        int prev = (currentIndex - 1 + playlist.size()) % playlist.size();
        playTrackAt(prev);
    }

    private void updatePlayPauseIcon() {
        playPauseButton.setImageResource(isPlaying
                ? android.R.drawable.ic_media_pause
                : android.R.drawable.ic_media_play);
    }

    // Sanftes Pulsieren des Cover-Platzhalters während der Wiedergabe
    private void startPulse() {
        stopPulse();
        pulseAnimator = ValueAnimator.ofFloat(1.0f, 1.06f);
        pulseAnimator.setDuration(900);
        pulseAnimator.setRepeatMode(ValueAnimator.REVERSE);
        pulseAnimator.setRepeatCount(ValueAnimator.INFINITE);
        pulseAnimator.setInterpolator(new LinearInterpolator());
        pulseAnimator.addUpdateListener(anim -> {
            float scale = (float) anim.getAnimatedValue();
            coverGlow.setScaleX(scale);
            coverGlow.setScaleY(scale);
        });
        pulseAnimator.start();
    }

    private void stopPulse() {
        if (pulseAnimator != null) {
            pulseAnimator.cancel();
        }
        coverGlow.setScaleX(1.0f);
        coverGlow.setScaleY(1.0f);
    }

    private void releasePlayer() {
        handler.removeCallbacks(progressUpdater);
        if (mediaPlayer != null) {
            mediaPlayer.release();
            mediaPlayer = null;
        }
    }

    private String formatTime(int ms) {
        int totalSec = ms / 1000;
        int min = totalSec / 60;
        int sec = totalSec % 60;
        return String.format(Locale.getDefault(), "%d:%02d", min, sec);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopPulse();
        releasePlayer();
    }
}
