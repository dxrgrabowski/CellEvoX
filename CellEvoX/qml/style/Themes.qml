pragma Singleton
import QtQuick 2.15

QtObject {
    readonly property color primary: "#1DB954"
    readonly property color background: "#121212"
    readonly property color surface: Qt.rgba(255, 255, 255, 0.05)
    readonly property color error: "#FF4444"
    readonly property color success: "#1DB954"
    
    readonly property int radiusSmall: 6
    readonly property int radiusMedium: 12
    readonly property int radiusLarge: 16
    
    readonly property int spacingSmall: 8
    readonly property int spacingMedium: 16
    readonly property int spacingLarge: 24
}