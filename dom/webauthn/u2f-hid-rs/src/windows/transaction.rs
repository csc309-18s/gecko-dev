/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

use platform::monitor::Monitor;
use runloop::RunLoop;
use std::io;
use util::{io_err, OnceCallback};

pub struct Transaction {
    // Handle to the thread loop.
    thread: Option<RunLoop>,
}

impl Transaction {
    pub fn new<F, T>(timeout: u64, callback: OnceCallback<T>, new_device_cb: F) -> io::Result<Self>
    where
        F: Fn(String, &Fn() -> bool) + Sync + Send + 'static,
        T: 'static,
    {
        let thread = RunLoop::new_with_timeout(
            move |alive| {
                // Create a new device monitor.
                let mut monitor = Monitor::new(new_device_cb);

                // Start polling for new devices.
                try_or!(monitor.run(alive), |e| callback.call(Err(e)));

                // Send an error, if the callback wasn't called already.
                callback.call(Err(io_err("aborted or timed out")));
            },
            timeout,
        )?;

        Ok(Self {
            thread: Some(thread),
        })
    }

    pub fn cancel(&mut self) {
        // This must never be None.
        self.thread.take().unwrap().cancel();
    }
}
