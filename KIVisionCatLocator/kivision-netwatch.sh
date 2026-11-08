#!/bin/bash
# WLAN-Waechter fuer den KIVision-Pi.
#
# 2026-10-23: Der Router lehnte die Anmeldung kurz ab (ASSOC-REJECT 16),
# NetworkManager gab nach drei Versuchen mit "no-secrets" auf und versuchte es
# danach NIE wieder - der Pi war 8 h offline, bis jemand den Stecker zog.
# Dieser Waechter pingt alle 30 s den Router:
#   - 3 min kein Kontakt -> Verbindung neu anstossen (hebt die NM-Sperre auf)
#   - 15 min kein Kontakt -> Pi neu starten
# Laeuft als root (kivision-netwatch.service), unabhaengig vom Kameraprogramm.
#
# 2026-10-24: Der Pi hoert im WLAN gar keine Broadcasts/Multicasts (0 Pakete,
# auch direkt nach Neuverbinden), also auch keine ARP-Anfragen. Laeuft der
# ARP-Eintrag des Managers nach ruhigen 5 min ab, fragt er per Broadcast neu
# und die Unicast-Weiterleitung (catObserved) geht ins Leere - 8:59 kein
# Radar-Bild. Deshalb fragt der Pi selbst alle STEP s beim Manager an (legt den
# Eintrag dort an) und kuendigt sich allen an (frischt bestehende auf, z.B. PC).
# Braucht iputils-arping.
#
# 2026-11-08: Die Verbindung ist fest an den WLAN-Repeater D-Link DIR-505 (192.168.0.146,
# BSSID C8:BE:19:5E:8E:74) gebunden: -49 statt -69 dBm, 52/72 statt 1/5,5 Mbit/s, und ueber
# den Repeater kommt sogar Multicast an. Faellt der Repeater aus, fand der Pi mit der Bindung
# NIE wieder ins Netz. Darum hebt der erste Neuanstoss (3 min weg) die Bindung auf - der Pi
# nimmt dann wieder irgendeinen "Rohan"-Zugangspunkt. Neu binden von Hand:
#   sudo nmcli con modify preconfigured 802-11-wireless.bssid C8:BE:19:5E:8E:74

CON=preconfigured
STEP=30
KICK_S=180
REBOOT_S=900
MANAGER=192.168.0.180

down=0
while true; do
    if command -v arping >/dev/null; then
        arping -q -c 1 -w 2 -I wlan0 "$MANAGER"
        arping -q -U -c 1 -I wlan0 "$(ip -4 -o addr show wlan0 | awk '{split($4,a,"/"); print a[1]; exit}')"
    fi
    gw=$(ip -4 route show default | awk '{print $3; exit}')
    if [ -n "$gw" ] && ping -c 2 -W 3 "$gw" >/dev/null 2>&1; then
        [ "$down" -gt 0 ] && echo "WLAN wieder da nach ${down} s"
        down=0
    else
        down=$((down + STEP))
        if [ "$down" -ge "$REBOOT_S" ]; then
            echo "WLAN seit ${down} s weg -> Neustart"
            systemctl reboot
            exit 0
        elif [ $((down % KICK_S)) -eq 0 ]; then
            echo "WLAN seit ${down} s weg -> nmcli con up $CON"
            bssid=$(nmcli -g 802-11-wireless.bssid con show "$CON" 2>/dev/null)
            if [ -n "$bssid" ]; then
                echo "Bindung an $bssid (Repeater?) aufgehoben"
                nmcli con modify "$CON" 802-11-wireless.bssid ""
            fi
            nmcli radio wifi on
            timeout 60 nmcli con up "$CON" || echo "nmcli con up fehlgeschlagen"
        fi
    fi
    sleep "$STEP"
done
