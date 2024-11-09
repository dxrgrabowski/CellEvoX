import QtQuick
import QtQuick.Controls
import QtQuick.Effects

Switch {
    id: control
    
    property color activeColor: "#1DB954"
    property color inactiveColor: "#ffffff30"
    
    indicator: Rectangle {
        id: backgroundRect
        implicitWidth: 56
        implicitHeight: 28
        x: control.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: control.checked ? activeColor : inactiveColor
        
        // Glass effect
        MultiEffect {
            source: backgroundRect
            blur: 0.5
            brightness: 0.1
            contrast: 0.1
        }

        Rectangle {
            id: thumb
            x: control.checked ? parent.width - width - 4 : 4
            y: 4
            width: parent.height - 8
            height: width
            radius: width / 2
            color: "#ffffff"
            
            // Shadow effect
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#80000000"
                shadowHorizontalOffset: 1
                shadowVerticalOffset: 1
                shadowBlur: 4.0
            }

            // Smooth transition animation
            Behavior on x {
                NumberAnimation {
                    duration: 200
                    easing.type: Easing.InOutQuad
                }
            }
        }

        // Hover effect
        Rectangle {
            anchors.fill: parent
            radius: parent.radius
            color: "#ffffff"
            opacity: control.hovered ? 0.1 : 0

            Behavior on opacity {
                NumberAnimation { duration: 200 }
            }
        }
    }

    // Label styling
    contentItem: Text {
        text: control.text
        font.pixelSize: 14
        color: "#ffffff"
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }

    // State transitions
    states: [
        State {
            name: "pressed"
            when: control.pressed
            PropertyChanges {
                target: thumb
                scale: 0.9
            }
        }
    ]

    transitions: [
        Transition {
            from: "*"
            to: "pressed"
            NumberAnimation {
                properties: "scale"
                duration: 100
                easing.type: Easing.InOutQuad
            }
        },
        Transition {
            from: "pressed"
            to: "*"
            NumberAnimation {
                properties: "scale"
                duration: 100
                easing.type: Easing.InOutQuad
            }
        }
    ]
}