pragma Singleton
import QtQuick

QtObject {
    readonly property color primary: "#1C1C1E"
    readonly property color secondary: "#2C2C2E"
    readonly property color accent: "#007AFF"
    readonly property color textPrimary: "#FFFFFF"
    readonly property color textSecondary: "#99FFFFFF"
    
    readonly property real defaultOpacity: 0.85
    readonly property real glassEffect: 0.65
    
    readonly property int radius: 12
    readonly property int spacing: 20
}
