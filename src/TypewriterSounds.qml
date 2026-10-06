import QtQuick
import QtMultimedia

// Plays a typewriter sound for each key the editor receives. The effects are
// only loaded while sounds are switched on, so the audio device stays closed
// otherwise.
Item {
    id: sounds

    property bool active: false
    property real volume: 0.5

    component Effect: SoundEffect {
        volume: sounds.volume
    }

    function keyPressed(event) {
        if (!active || event.isAutoRepeat || !player.item)
            return;

        if (event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier))
            return;

        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
            player.item.play(player.item.returns);
        else if (event.key === Qt.Key_Backspace || event.key === Qt.Key_Delete)
            player.item.play(player.item.backspaces);
        else if (event.key === Qt.Key_Space)
            player.item.play(player.item.spaces);
        else if (event.text.length > 0)
            player.item.play(player.item.keys);
    }

    Loader {
        id: player
        active: sounds.active
        sourceComponent: Item {
            // Several copies of each effect, because replaying a SoundEffect
            // that is still sounding cuts it off.
            readonly property var keys: [key1a, key2a, key3a, key4a, key1b, key2b, key3b, key4b]
            readonly property var spaces: [spaceA, spaceB]
            readonly property var backspaces: [backspaceA, backspaceB]
            readonly property var returns: [returnA]
            property var nextIndex: ({})

            function play(effects) {
                var name = effects[0].objectName;
                var index = nextIndex[name] || 0;
                // Strike a different key sample each time so fast typing
                // does not sound like a loop.
                if (effects === keys)
                    index = (index + 1 + Math.floor(Math.random() * 3)) % effects.length;
                else
                    index = (index + 1) % effects.length;
                nextIndex[name] = index;
                effects[index].play();
            }

            Effect { id: key1a; objectName: "key"; source: "qrc:/sounds/key1.wav" }
            Effect { id: key2a; source: "qrc:/sounds/key2.wav" }
            Effect { id: key3a; source: "qrc:/sounds/key3.wav" }
            Effect { id: key4a; source: "qrc:/sounds/key4.wav" }
            Effect { id: key1b; source: "qrc:/sounds/key1.wav" }
            Effect { id: key2b; source: "qrc:/sounds/key2.wav" }
            Effect { id: key3b; source: "qrc:/sounds/key3.wav" }
            Effect { id: key4b; source: "qrc:/sounds/key4.wav" }
            Effect { id: spaceA; objectName: "space"; source: "qrc:/sounds/space.wav" }
            Effect { id: spaceB; source: "qrc:/sounds/space.wav" }
            Effect { id: backspaceA; objectName: "backspace"; source: "qrc:/sounds/backspace.wav" }
            Effect { id: backspaceB; source: "qrc:/sounds/backspace.wav" }
            Effect { id: returnA; objectName: "return"; source: "qrc:/sounds/return.wav"; volume: sounds.volume * 0.8 }
        }
    }
}
