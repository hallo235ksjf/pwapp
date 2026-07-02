package com.raheel.player;

import android.graphics.Color;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.RecyclerView;

import java.util.List;

public class TrackAdapter extends RecyclerView.Adapter<TrackAdapter.TrackViewHolder> {

    public interface OnTrackClickListener {
        void onTrackClick(int position);
    }

    private final List<Track> tracks;
    private final OnTrackClickListener listener;
    private int playingPosition = -1;

    public TrackAdapter(List<Track> tracks, OnTrackClickListener listener) {
        this.tracks = tracks;
        this.listener = listener;
    }

    public void setPlayingPosition(int position) {
        int old = playingPosition;
        playingPosition = position;
        if (old >= 0) notifyItemChanged(old);
        if (position >= 0) notifyItemChanged(position);
    }

    @NonNull
    @Override
    public TrackViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View v = LayoutInflater.from(parent.getContext())
                .inflate(R.layout.item_track, parent, false);
        return new TrackViewHolder(v);
    }

    @Override
    public void onBindViewHolder(@NonNull TrackViewHolder holder, int position) {
        Track t = tracks.get(position);
        holder.title.setText(t.title);
        holder.artist.setText(t.artist);

        boolean isPlaying = position == playingPosition;
        holder.title.setTextColor(isPlaying ? Color.parseColor("#B388FF") : Color.parseColor("#F2F2F5"));
        holder.playingIcon.setVisibility(isPlaying ? View.VISIBLE : View.GONE);

        holder.itemView.setOnClickListener(v -> listener.onTrackClick(position));
    }

    @Override
    public int getItemCount() {
        return tracks.size();
    }

    static class TrackViewHolder extends RecyclerView.ViewHolder {
        TextView title;
        TextView artist;
        ImageView playingIcon;

        TrackViewHolder(View itemView) {
            super(itemView);
            title = itemView.findViewById(R.id.trackTitle);
            artist = itemView.findViewById(R.id.trackArtist);
            playingIcon = itemView.findViewById(R.id.playingIcon);
        }
    }
}
