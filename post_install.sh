post_install() {
    HAS_KDE=0
    HAS_GNOME_OR_OTHER=0

    if command -v kwin_wayland >/dev/null 2>&1 || command -v kwin_x11 >/dev/null 2>&1; then HAS_KDE=1; fi
    if command -v gnome-shell >/dev/null 2>&1 || command -v xfce4-session >/dev/null 2>&1; then HAS_GNOME_OR_OTHER=1; fi

    for d in /home/*; do
        if [ -d "$d" ]; then
            user=$(basename "$d")
            
            mkdir -p "$d/.config/autostart"
            echo -e "[Desktop Entry]\nHidden=true" > "$d/.config/autostart/org.fcitx.Fcitx5.desktop"
            chown "$user" "$d/.config/autostart/org.fcitx.Fcitx5.desktop" 2>/dev/null || true

            if [ "$HAS_KDE" -eq 1 ]; then
                sed -i 's/^export.*fcitx/#&/g' "$d/.bashrc" "$d/.profile" "$d/.xprofile" "$d/.bash_profile" 2>/dev/null || true
                sed -i 's/^export.*ibus/#&/g' "$d/.bashrc" "$d/.profile" "$d/.xprofile" "$d/.bash_profile" 2>/dev/null || true

                mkdir -p "$d/.config/environment.d"
                cat << 'ENVEOF' > "$d/.config/environment.d/99-unikey-wayland.conf"
GTK_IM_MODULE=wayland
QT_IM_MODULE=wayland
XMODIFIERS=@im=wayland
ENVEOF
                chown -R "$user" "$d/.config/environment.d" 2>/dev/null || true
                if command -v flatpak >/dev/null 2>&1; then
                    su - "$user" -c 'flatpak override --user --env=GTK_IM_MODULE=wayland --env=QT_IM_MODULE=wayland --env=XMODIFIERS=@im=wayland' 2>/dev/null || true
                fi
            fi
            
            if [ "$HAS_GNOME_OR_OTHER" -eq 1 ] || [ "$HAS_KDE" -eq 0 ]; then
                su - "$user" -c 'env DCONF_PROFILE=ibus dconf write /desktop/ibus/general/preload-engines "[\"unikey-wayland\"]"' 2>/dev/null || true
                su - "$user" -c 'gsettings set org.gnome.desktop.input-sources sources "[(\"xkb\", \"us\"), (\"ibus\", \"unikey-wayland\")]"' 2>/dev/null || true
            fi
        fi
    done

    if [ "$HAS_KDE" -eq 1 ]; then
        sed -i 's/^export.*fcitx/#&/g' /etc/profile.d/*.sh 2>/dev/null || true
        sed -i 's/^export.*ibus/#&/g' /etc/profile.d/*.sh 2>/dev/null || true
    fi
}
