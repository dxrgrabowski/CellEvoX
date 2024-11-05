import QtQuick 2.15
import QtQuick.Controls 1.10
import QtQuick.Effects 1.0

Rectangle {
    color: Theme.secondary
    opacity: Theme.glassEffect
    radius: Theme.radius
    
    layer.enabled: true
    MultiEffect {
        blur: 1.0
        blurMax: 32
        brightness: 0.1
    }
}