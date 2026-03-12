import QtQuick
import QtQuick.Effects

Rectangle {
    id: root
    color: Qt.rgba(255, 255, 255, 0.03)
    radius: 12

    layer.enabled: true
    layer.effect: MultiEffect {
        blur: 1.0
        brightness: 0.1
        contrast: 0.1
    }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(255, 255, 255, 0.05) }
            GradientStop { position: 1.0; color: Qt.rgba(255, 255, 255, 0.02) }
        }
    }
}