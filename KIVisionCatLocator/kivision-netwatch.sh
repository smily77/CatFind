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

CON=preconfigured
STEP=30
KICK_S=180
REBOOT_S=900

down=0
while true; do
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
            nmcli radio wifi on
            timeout 60 nmcli con up "$CON" || echo "nmcli con up fehlgeschlagen"
        fi
    fi
    sleep "$STEP"
done
