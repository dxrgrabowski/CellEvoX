import QtQuick
import QtQuick.Controls
import QtQuick.Effects

ProgressBar {
    id: control
    
    property color progressColor: "#1DB954"
    property color backgroundColor: "#ffffff15"
    property bool showPercentage: true
    
    height: 24
    padding: 2
    
    background: Rectangle {
        implicitWidth: 200
        implicitHeight: control.height
        color: control.backgroundColor
        radius: height / 2
        
        // Glass effect
        MultiEffect {
            source: parent
            blur: 0.5
            brightness: 0.1
            contrast: 0.1
        }
    }
    
    contentItem: Item {
        implicitWidth: 200
        implicitHeight: control.height - control.padding * 2

        Rectangle {
            id: progressRect
            width: control.visualPosition * parent.width
            height: parent.height
            radius: height / 2
            color: control.progressColor
            
            // Gradient overlay
            Rectangle {
                anchors.fill: parent
                radius: parent.radius
                gradient: Gradient {
                    GradientStop { position: 0.0; color: "#00ffffff" }
                    GradientStop { position: 0.5; color: "#15ffffff" }
                    GradientStop { position: 1.0; color: "#00ffffff" }
                }
            }

            // Animated shine effect
            Rectangle {
                id: shine
                width: parent.width * 0.3
                height: parent.height
                color: "#ffffff"
                opacity: 0.2
                radius: parent.radius
                x: -width

                NumberAnimation on x {
                    from: -shine.width
                    to: progressRect.width
                    duration: 1500
                    loops: Animation.Infinite
                    running: control.value > 0 && control.value < 1
                }
            }
        }

        // Percentage text
        Text {
            anchors.centerIn: parent
            text: Math.round(control.value * 100) + "%"
            color: "#ffffff"
            font.pixelSize: 12
            visible: control.showPercentage
            opacity: control.value > 0 ? 1 : 0

            Behavior on opacity {
                NumberAnimation { duration: 200 }
            }
        }
    }

    // Indeterminate animation
    Rectangle {
        id: indeterminateBar
        visible: control.indeterminate
        height: control.height - control.padding * 2
        width: parent.width * 0.3
        radius: height / 2
        x: -width
        color: control.progressColor

        SequentialAnimation on x {
            running: control.indeterminate
            loops: Animation.Infinite
            NumberAnimation {
                from: -indeterminateBar.width
                to: control.width
                duration: 1500
                easing.type: Easing.InOutQuad
            }
            PauseAnimation { duration: 100 }
        }
    }

    // Hover effect
    Rectangle {
        anchors.fill: parent
        radius: height / 2
        color: "#ffffff"
        opacity: control.hovered ? 0.1 : 0

        Behavior on opacity {
            NumberAnimation { duration: 200 }
        }
    }
}