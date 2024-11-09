import QtQuick
import QtQuick.Controls
import QtQuick.Effects

import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import Qt.Quick.Effects

Rectangle {
    id: root
    height: 30
    radius: 15
    color: "#333333"

    property real progress: 0.0
    property string statusText: "Ready"

    MultiEffect {
        source: parent
        blur: 0.3
        brightness: 0.05
    }

    Rectangle {
        id: progressFill
        width: parent.width * progress
        height: parent.height
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#4CAF50" }
            GradientStop { position: 1.0; color: "#45a049" }
        }

        Behavior on width {
            NumberAnimation { 
                duration: 300
                easing.type: Easing.InOutQuad
            }
        }
    }

    Text {
        anchors.centerIn: parent
        text: Math.round(progress * 100) + "% - " + statusText
        color: "#ffffff"
        font.pixelSize: 14
        font.weight: Font.Medium
    }
}
