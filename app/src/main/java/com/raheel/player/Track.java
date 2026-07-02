package com.raheel.player;

public class Track {
    public final long id;
    public final String title;
    public final String artist;
    public final long durationMs;

    public Track(long id, String title, String artist, long durationMs) {
        this.id = id;
        this.title = (title == null || title.isEmpty()) ? "Unbekannter Titel" : title;
        this.artist = (artist == null || artist.isEmpty()) ? "Unbekannter Interpret" : artist;
        this.durationMs = durationMs;
    }
}
