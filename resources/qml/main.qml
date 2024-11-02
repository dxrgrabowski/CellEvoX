import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import "components" as Components

ApplicationWindow {
    id: window
    width: 1920
    height: 1080
    visible: true
    title: "CellEvoX"
    color: Theme.primary

    RowLayout {
        anchors.fill: parent
        spacing: Theme.spacing

        // Lewy panel kontrolny
        Components.GlassPanel {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            Layout.margins: Theme.spacing
        }

        // Główny widok symulacji
        Components.GlassPanel {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: Theme.spacing
        }

        // Panel informacyjny
        Components.GlassPanel {
            Layout.preferredWidth: 350
            Layout.fillHeight: true
            Layout.margins: Theme.spacing
        }
    }
}
