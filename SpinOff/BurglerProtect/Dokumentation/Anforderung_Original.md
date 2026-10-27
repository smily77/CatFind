# Spin-off: Besucherabhängige Lichtsteuerung zur Einbruchprävention

## 1. Ausgangslage und Zielsetzung

In den Wintermonaten kommt es vermehrt zu sogenannten **Dämmerungseinbrüchen**. Eine mögliche Präventionsmassnahme besteht darin, bei der Annäherung einer Person an das Haus automatisch eine Beleuchtung einzuschalten.

Besonders wirkungsvoll könnte es sein, nicht die Aussenbeleuchtung, sondern das Licht im Wohnzimmer einzuschalten. Dadurch entsteht der Eindruck, dass sich jemand im Haus aufhält.

Damit diese Funktion die Bewohner nicht stört, soll sie nur bei Bedarf aktiviert werden können. Dazu dient ein separater Taster, mit dem sich das System **scharfstellen und deaktivieren** lässt. Ausserdem soll das System keine Lichtschaltung auslösen, wenn das Wohnzimmerlicht bereits eingeschaltet ist.

## 2. Erkennung von Besuchern

Im bestehenden System kann der **DomeC1** Bewegungen beziehungsweise Objekte auf der Veranda erfassen.

Ein möglicher Besucher wird anhand der bereits vorhandenen Ereignisse `CatObserved` erkannt, sofern sich die ermittelten Koordinaten im folgenden Quadranten befinden:

- **X > 0**
- **Y < 0**

Alle Ereignisse in diesem Quadranten sollen als potenzielle Besucherereignisse behandelt werden.

Da nicht jede Erkennung automatisch eine relevante Annäherung an das Haus darstellt, soll zusätzlich ein räumlich definierter Bereich festgelegt werden, innerhalb dessen eine Erkennung tatsächlich die Lichtsteuerung auslösen kann.

Um Fehlauslösungen durch einzelne, möglicherweise fehlerhafte Messungen zu vermeiden, soll **ein einzelnes `CatObserved`-Ereignis nicht ausreichen**, um das Licht einzuschalten.

Stattdessen sollen mehrere zeitlich zusammenhängende und möglichst demselben Objekt zuordenbare Erkennungen erforderlich sein. Anzahl und Zeitfenster sollen anhand der tatsächlichen Ereignisfrequenz und der Erfahrungen mit dem bestehenden Sensorsystem sinnvoll gewählt werden. Dabei ist ein ausgewogenes Verhältnis zwischen zuverlässiger Erkennung, kurzer Reaktionszeit und Vermeidung von Fehlauslösungen anzustreben.

## 3. Erweiterung des VPS um den Tab «Besucher»

Zur Visualisierung und Konfiguration soll der VPS um einen neuen Tab mit der Bezeichnung **«Besucher»** erweitert werden.

Dieser Tab soll folgende Funktionen bereitstellen:

### 3.1 Grafische Darstellung

Alle erkannten Ereignisse im definierten Quadranten (X > 0, Y < 0) sollen grafisch dargestellt werden.

Die Darstellung soll sich am bestehenden Tab `Welt_Karte` orientieren und die Positionen der erkannten Objekte im gemeinsamen Koordinatensystem anzeigen.

### 3.2 Ereignisliste

Zusätzlich sollen sämtliche Besucherereignisse in einer Liste angezeigt werden.

Aufbau und Funktionalität sollen sich am bestehenden Tab `Liste` orientieren.

### 3.3 Löschen der Besucherereignisse

Der Tab soll über eine eigene Schaltfläche zum Löschen der aufgezeichneten Besucherereignisse verfügen.

Diese Funktion soll analog zur bestehenden Schaltfläche `Karte zurücksetzen` arbeiten, jedoch **ausschliesslich die Daten des Tabs «Besucher» löschen**.

Die übrigen Daten und Darstellungen des Gesamtsystems dürfen dadurch nicht verändert werden.

### 3.4 Definition des Auslösebereichs

Innerhalb der grafischen Darstellung soll ein **editierbares Polygon** eingezeichnet werden können.

Dieses Polygon definiert den räumlichen Bereich, in dem eine Erkennung das Einschalten des Wohnzimmerlichts auslösen darf.

Das Polygon muss gespeichert und bei Bedarf nachträglich angepasst werden können.

### 3.5 Aktivierung und Deaktivierung

Auf dem Tab «Besucher» soll eine Schaltfläche vorhanden sein, über die sich die Lichtsteuerung scharfstellen beziehungsweise deaktivieren lässt.

Der aktuelle Zustand muss jederzeit sichtbar sein und mit dem Zustand des physischen Tasters übereinstimmen.

### 3.6 Konfiguration der Erkennung

Die Parameter zur Vermeidung von Fehlauslösungen sollen sinnvoll gewählt und bei Bedarf anpassbar sein:

- Mindestanzahl zusammenhängender gültiger Erkennungen
- Zeitfenster für die Auswertung
- Einschaltdauer des Wohnzimmerlichts

Die gewählten Einstellungen werden an das lokale Steuergerät übertragen und dort dauerhaft gespeichert.

## 4. Physischer Taster mit Statusanzeige

Zur lokalen Bedienung wird ein separater Taster eingesetzt.

Dieser verfügt über eine **vollflächige Beleuchtung**, die den aktuellen Betriebszustand anzeigt:

- **Beleuchtung ein:** System ist scharfgestellt.
- **Beleuchtung aus:** System ist deaktiviert.

Durch Betätigen des Tasters wird zwischen den beiden Zuständen gewechselt.

Die Bedienung soll sowohl am physischen Taster als auch über den VPS möglich sein. Beide Bedienmöglichkeiten müssen denselben Systemzustand verwenden und entsprechend synchronisiert werden.

## 5. Automatische Lichtsteuerung über Philips Hue

Die eigentliche Steuerlogik soll lokal auf dem Gerät mit dem physischen Taster ausgeführt werden.

Dieses Gerät empfängt die relevanten Erkennungsereignisse, überprüft deren Position und entscheidet selbstständig, ob das Wohnzimmerlicht eingeschaltet werden soll.

Eine Lichtschaltung erfolgt nur, wenn **alle folgenden Bedingungen erfüllt sind**:

1. Das System ist scharfgestellt.
2. Es liegen mehrere zeitlich zusammenhängende, plausible `CatObserved`-Erkennungen vor, die eine tatsächliche Anwesenheit wahrscheinlich machen.
3. Die Koordinaten dieser Erkennungen befinden sich innerhalb des definierten Auslösepolygons.
4. Das Wohnzimmerlicht ist zum Zeitpunkt der Auslösung ausgeschaltet.

Sind alle Bedingungen erfüllt, wird das Wohnzimmerlicht über **Philips Hue** für eine konfigurierbare Zeit eingeschaltet.

Nach Ablauf dieser Zeit soll das vom System eingeschaltete Licht wieder ausgeschaltet werden.

Dabei ist sicherzustellen, dass eine zwischenzeitliche manuelle Bedienung durch die Bewohner berücksichtigt wird, sodass das System nicht unbeabsichtigt ein manuell eingeschaltetes Licht ausschaltet.

Nach einer erfolgreichen Auslösung muss verhindert werden, dass dieselben Erkennungen unmittelbar eine weitere Lichtschaltung auslösen.

## 6. Unabhängigkeit vom VPS

Ein wesentliches Ziel besteht darin, dass die eigentliche Besucher- und Lichtsteuerung **auch ohne laufenden oder erreichbaren VPS funktioniert**.

Die Aufgaben werden deshalb wie folgt aufgeteilt:

**VPS – Konfiguration und Visualisierung**

- Darstellung und Protokollierung der Besucherereignisse
- Grafische Definition und Bearbeitung des Auslösepolygons
- Konfiguration der Erkennungsparameter
- Konfiguration der Einschaltdauer
- Anzeige des aktuellen Betriebszustands
- Scharfstellen und Deaktivieren als alternative Bedienmöglichkeit
- Übertragung der Konfiguration an das lokale Steuergerät

**Lokales Steuergerät mit Taster – autonome Steuerung**

- Empfang der relevanten Erkennungsereignisse
- Prüfung, ob sich die Erkennungen innerhalb des gespeicherten Polygons befinden
- Plausibilisierung mehrerer Erkennungen zur Vermeidung von Fehlauslösungen
- Verwaltung des Aktivierungszustands
- Anzeige des Betriebszustands über die Tasterbeleuchtung
- Abfrage des aktuellen Zustands der Wohnzimmerbeleuchtung
- Ein- und Ausschalten des Wohnzimmerlichts über Philips Hue
- Verwaltung der Einschaltdauer und Vermeidung mehrfacher Auslösungen

Die erforderliche Konfiguration, insbesondere das Auslösepolygon, die Erkennungsparameter und die Einschaltdauer, muss lokal gespeichert werden.

Dadurch bleibt das System auch dann funktionsfähig, wenn der VPS nicht verfügbar ist, sofern die Erkennungsereignisse das lokale Steuergerät weiterhin erreichen.

## 7. Zusammenfassung

Mit diesem Spin-off wird die bestehende Sensorinfrastruktur um eine Funktion zur Einbruchprävention erweitert.

Der DomeC1 übernimmt die Erkennung potenzieller Besucher auf der Veranda. Der VPS ermöglicht die grafische Auswertung, die Konfiguration des relevanten Überwachungsbereichs sowie die Bedienung.

Um Fehlauslösungen zu reduzieren, werden nicht einzelne Ereignisse, sondern mehrere zeitlich und räumlich plausible Erkennungen ausgewertet. Die dafür erforderlichen Parameter sollen auf Basis der vorhandenen Sensorcharakteristik und praktischer Erfahrungen sinnvoll festgelegt werden.

Die eigentliche Entscheidung und Lichtsteuerung erfolgen autonom auf einem lokalen Steuergerät mit beleuchtetem Taster.

**Das zentrale Konzept besteht darin, die vorhandenen Sensordaten für eine gezielte, räumlich begrenzte und nur bei Bedarf aktivierte Anwesenheitssimulation zu nutzen, ohne dass der VPS für den laufenden Betrieb erforderlich ist.**