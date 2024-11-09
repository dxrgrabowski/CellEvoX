// main.qml
import QtQuick 
import QtQuick.Controls 
import QtQuick.Window 
import QtQuick.Dialogs 
import QtQuick.Layouts
import Qt5Compat.GraphicalEffects
import CellEvoX 1.0
import "components" as Components
import QtQuick.Controls.Material
import QtQuick.Effects
//import Qt.labs.platform 1.1 as Platform



ApplicationWindow {
    id: root
    visible: true
    width: 1280
    height: 800
    title: "Scientific Simulation Interface"
    color: "#121212"

    // Custom fonts
    FontLoader { id: robotoLight; source: "qrc:/fonts/Roboto-Light.ttf" }
    FontLoader { id: robotoMedium; source: "qrc:/fonts/Roboto-Medium.ttf" }

    // Properties
    property bool isSimulationRunning: false
    property bool isDeterministic: true
    property real simulationProgress: 0.0

    // Main content
    RowLayout {
        anchors.fill: parent
        spacing: 20

        // Left panel - Parameters
        Components.GlassPanel {
            Layout.preferredWidth: 400
            Layout.fillHeight: true
            Layout.margins: 20

            ColumnLayout {
                anchors.fill: parent
                spacing: 15

                // Title
                Text {
                    text: "Simulation Parameters"
                    font.family: robotoMedium.name
                    font.pixelSize: 24
                    color: "#ffffff"
                }

                // Parameter inputs
                Repeater {
                    model: ListModel {
                        ListElement { name: "N"; label: "Population Size"; placeholder: "Enter N..." }
                        ListElement { name: "Nc"; label: "Critical Population"; placeholder: "Enter Nc..." }
                        ListElement { name: "effect"; label: "Effect Size"; placeholder: "Enter effect..." }
                        ListElement { name: "prob"; label: "Probability"; placeholder: "Enter prob..." }
                        ListElement { name: "tauStep"; label: "Tau Step"; placeholder: "Enter tau..." }
                        ListElement { name: "nP"; label: "Parameter N"; placeholder: "Enter nP..." }
                        ListElement { name: "nT"; label: "Time Steps"; placeholder: "Enter nT..." }
                    }

                    Components.ModernTextField {
                        Layout.fillWidth: true
                        label: model.label
                        placeholderText: model.placeholder
                    }
                }

                // Simulation type switch
                Components.ModelSwitch {
                    id: simulationTypeSwitch
                    text: "Simulation Type"
                    checked: isDeterministic
                    onCheckedChanged: isDeterministic = checked
                }

                // File operations
                RowLayout {
                    spacing: 10
                    Components.ModernButton {
                        text: "Load Parameters"
                        onClicked: fileDialog.open()
                    }
                    Components.ModernButton {
                        text: "Save Configuration"
                        onClicked: saveConfiguration()
                    }
                }

                // Start simulation button
                Components.ModernButton {
                    Layout.fillWidth: true
                    text: "Start Simulation"
                    enabled: !isSimulationRunning
                    onClicked: startSimulation()
                }

                // Progress bar
                Components.ProgressBar {
                    id: progressBar
                    Layout.fillWidth: true
                    value: simulationProgress
                }
            }
        }

        // Right panel - Results
        Components.GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 20

            visible: !isSimulationRunning

            ColumnLayout {
                anchors.fill: parent
                spacing: 15

                Text {
                    text: "Simulation Results"
                    font.family: robotoMedium.name
                    font.pixelSize: 24
                    color: "#ffffff"
                }

                // Graph area placeholder
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "transparent"
                    border.color: "#ffffff30"
                    border.width: 1
                }
            }
        }
    }

    // // File Dialog
    // Components.FileOperations {
    //     id: fileDialog
    //     title: "Load Parameters"
    //     nameFilters: ["Parameter files (*.json)"]
    //     onAccepted: loadParameters(selectedFile)
    // }
}