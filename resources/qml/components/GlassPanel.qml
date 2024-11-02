import QtQuick 2.15
import QtQuick.Controls
import QtQuick.Effects 

Rectangle {
    color: Theme.secondary
    opacity: Theme.glassEffect
    radius: Theme.radius
    
    layer.enabled: true
    layer.effect: MultiEffect {
        blur: 1.0
        blurMax: 32
        brightness: 0.1
    }
}