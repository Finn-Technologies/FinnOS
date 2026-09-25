//! Normalized semantic input events shared by Peony surfaces.

/// A semantic key independent of the physical device scancode format.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum Key {
    /// A printable ASCII key.
    Character(char),
    /// Escape key.
    Escape,
    /// Return/enter key.
    Enter,
    /// Backspace key.
    Backspace,
    /// Tab key.
    Tab,
    /// Left arrow key.
    Left,
    /// Right arrow key.
    Right,
    /// Up arrow key.
    Up,
    /// Down arrow key.
    Down,
    /// Left or right Shift modifier key.
    Shift,
    /// Control modifier key.
    Control,
    /// Alt/Option modifier key.
    Alt,
    /// Super/Windows modifier key.
    Super,
    /// Any other key that is not part of the desktop shortcut model.
    Other,
}

/// Whether a normalized key event is a press or release.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum KeyState {
    /// The key transitioned down.
    Pressed,
    /// The key transitioned up.
    Released,
}

/// A normalized keyboard event.
#[allow(clippy::struct_excessive_bools)]
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub struct KeyboardEvent {
    /// Semantic key identity.
    pub key: Key,
    /// Press/release transition.
    pub state: KeyState,
    /// Whether Shift was active when the event was produced.
    pub shift: bool,
    /// Whether Control was active when the event was produced.
    pub control: bool,
    /// Whether Alt was active when the event was produced.
    pub alt: bool,
    /// Whether Super was active when the event was produced.
    pub super_key: bool,
}

impl KeyboardEvent {
    /// Return whether this is a key-down event.
    #[must_use]
    pub const fn is_pressed(&self) -> bool {
        matches!(self.state, KeyState::Pressed)
    }

    /// Return whether this event carries the platform launcher shortcut.
    #[must_use]
    pub const fn is_launcher_shortcut(&self) -> bool {
        self.is_pressed() && matches!(self.key, Key::Super) && self.super_key
    }

    /// Return whether this event carries the terminal shortcut.
    #[must_use]
    pub const fn is_terminal_shortcut(&self) -> bool {
        self.is_pressed() && self.control && self.alt && matches!(self.key, Key::Character('T'))
    }

    /// Return whether this event carries the window-close shortcut.
    #[must_use]
    pub const fn is_close_shortcut(&self) -> bool {
        self.is_pressed() && self.alt && matches!(self.key, Key::Character('F' | 'f'))
    }

    /// Return whether this event carries the task-switch shortcut.
    #[must_use]
    pub const fn is_task_switch_shortcut(&self) -> bool {
        self.is_pressed() && self.alt && matches!(self.key, Key::Tab)
    }
}

#[cfg(test)]
mod tests {
    use super::{Key, KeyState, KeyboardEvent};

    #[allow(clippy::fn_params_excessive_bools)]
    const fn key(
        key: Key,
        shift: bool,
        control: bool,
        alt: bool,
        super_key: bool,
    ) -> KeyboardEvent {
        KeyboardEvent {
            key,
            state: KeyState::Pressed,
            shift,
            control,
            alt,
            super_key,
        }
    }

    #[test]
    fn shortcut_predicates_require_pressed_state() {
        let event = key(Key::Super, false, false, false, true);
        assert!(event.is_launcher_shortcut());
        let mut released = event;
        released.state = KeyState::Released;
        assert!(!released.is_launcher_shortcut());
        assert!(key(Key::Character('T'), false, true, true, false).is_terminal_shortcut());
        assert!(key(Key::Character('F'), false, false, true, false).is_close_shortcut());
        assert!(key(Key::Tab, false, false, true, false).is_task_switch_shortcut());
    }
}
