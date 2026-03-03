use gilrs::{Button, EventType, Gilrs};
use std::sync::mpsc::{self, Receiver};
use std::thread;
use std::time::{Duration, Instant};

const PAIR_WINDOW_MS: f64 = 50.0;
/// Poll gilrs every 0.125ms (~8kHz) — fast enough to separate events
/// across different USB frames (1ms apart) while keeping CPU usage low.
const POLL_INTERVAL: Duration = Duration::from_micros(125);

pub struct ButtonPair {
    pub button_a: Button,
    pub button_b: Button,
    pub gap_ms: f64,
}

#[derive(Clone)]
pub enum InputEvent {
    Pressed(Button),
    Released(Button),
}

enum InputMsg {
    Pressed(Button, Instant),
    Released(Button),
    GamepadName(Option<String>),
}

struct PendingPress {
    button: Button,
    timestamp: Instant,
}

pub struct GamepadInput {
    rx: Receiver<InputMsg>,
    pending: Option<PendingPress>,
    gamepad_name: Option<String>,
}

impl GamepadInput {
    pub fn new() -> Result<Self, String> {
        let (init_tx, init_rx) = mpsc::sync_channel(1);
        let (tx, rx) = mpsc::channel();

        thread::spawn(move || {
            let mut gilrs = match Gilrs::new() {
                Ok(g) => {
                    let _ = init_tx.send(Ok(()));
                    g
                }
                Err(e) => {
                    let _ = init_tx.send(Err(e.to_string()));
                    return;
                }
            };

            // Force immediate gamepad name check on first iteration
            let mut last_name_check = Instant::now() - Duration::from_secs(10);

            loop {
                // Periodically send gamepad connection status
                if last_name_check.elapsed() >= Duration::from_secs(1) {
                    let name = gilrs
                        .gamepads()
                        .next()
                        .map(|(_, gp)| gp.name().to_string());
                    if tx.send(InputMsg::GamepadName(name)).is_err() {
                        return; // UI thread dropped, exit
                    }
                    last_name_check = Instant::now();
                }

                // Process events — only ONE ButtonPressed per cycle.
                // When gilrs (XInput) detects two state changes in one poll,
                // it buffers both events. By taking only one press and sleeping,
                // the second press gets its own timestamp on the next cycle.
                // Events from different USB frames naturally get different
                // timestamps because gilrs re-polls the OS when the buffer
                // is empty.
                loop {
                    match gilrs.next_event() {
                        Some(event) => match event.event {
                            EventType::ButtonPressed(button, _) => {
                                if tx
                                    .send(InputMsg::Pressed(button, Instant::now()))
                                    .is_err()
                                {
                                    return;
                                }
                                break; // One press per cycle
                            }
                            EventType::ButtonReleased(button, _) => {
                                if tx.send(InputMsg::Released(button)).is_err() {
                                    return;
                                }
                            }
                            _ => {}
                        },
                        None => break, // No more events
                    }
                }

                thread::sleep(POLL_INTERVAL);
            }
        });

        // Wait for init result from thread
        match init_rx.recv() {
            Ok(Ok(())) => Ok(Self {
                rx,
                pending: None,
                gamepad_name: None,
            }),
            Ok(Err(e)) => Err(e),
            Err(_) => Err("Input thread died during init".to_string()),
        }
    }

    /// Receive timestamped events from the input thread and detect pairs.
    pub fn poll(&mut self) -> (Option<ButtonPair>, Vec<InputEvent>) {
        let mut pair = None;
        let mut events = Vec::new();

        while let Ok(msg) = self.rx.try_recv() {
            match msg {
                InputMsg::Pressed(button, timestamp) => {
                    events.push(InputEvent::Pressed(button));

                    match self.pending.take() {
                        None => {
                            self.pending = Some(PendingPress { button, timestamp });
                        }
                        Some(pending) => {
                            let gap_ms = timestamp
                                .duration_since(pending.timestamp)
                                .as_secs_f64()
                                * 1000.0;

                            if gap_ms <= PAIR_WINDOW_MS {
                                pair = Some(ButtonPair {
                                    button_a: pending.button,
                                    button_b: button,
                                    gap_ms,
                                });
                            } else {
                                self.pending = Some(PendingPress { button, timestamp });
                            }
                        }
                    }
                }
                InputMsg::Released(button) => {
                    events.push(InputEvent::Released(button));
                }
                InputMsg::GamepadName(name) => {
                    self.gamepad_name = name;
                }
            }
        }

        // Expire stale pending press
        if let Some(ref pending) = self.pending {
            if pending.timestamp.elapsed().as_secs_f64() * 1000.0 > PAIR_WINDOW_MS {
                self.pending = None;
            }
        }

        (pair, events)
    }

    pub fn connected_gamepad_name(&self) -> Option<String> {
        self.gamepad_name.clone()
    }
}

pub fn format_button(button: Button) -> String {
    match button {
        Button::South => "A/Cross".to_string(),
        Button::East => "B/Circle".to_string(),
        Button::West => "X/Square".to_string(),
        Button::North => "Y/Triangle".to_string(),
        Button::LeftTrigger => "LB/L1".to_string(),
        Button::RightTrigger => "RB/R1".to_string(),
        Button::LeftTrigger2 => "LT/L2".to_string(),
        Button::RightTrigger2 => "RT/R2".to_string(),
        Button::LeftThumb => "LS".to_string(),
        Button::RightThumb => "RS".to_string(),
        Button::Select => "Back/Select".to_string(),
        Button::Start => "Start".to_string(),
        Button::DPadUp => "DPad Up".to_string(),
        Button::DPadDown => "DPad Down".to_string(),
        Button::DPadLeft => "DPad Left".to_string(),
        Button::DPadRight => "DPad Right".to_string(),
        other => format!("{:?}", other),
    }
}
