#!/usr/bin/env python3
import gi
gi.require_version("Gtk", "3.0")
from gi.repository import Gtk
import socket

class DenonControl(Gtk.Window):
    def __init__(self):
        super().__init__(title="Denon AVR-X1400H Control")
        self.set_border_width(10)

        # Replace with your AVR's IP address
        self.avr_ip = "192.168.1.246"
        self.avr_port = 23

        grid = Gtk.Grid()
        grid.set_row_spacing(10)     # Set spacing between rows
        grid.set_column_spacing(10)  # Set spacing between columns
        self.add(grid)

        # Power On Button
        power_on_button = Gtk.Button(label="Power On")
        power_on_button.connect("clicked", self.on_power_on)
        power_on_button.set_margin_top(5)
        grid.attach(power_on_button, 0, 0, 1, 1)

        # Power Off Button
        power_off_button = Gtk.Button(label="Power Off")
        power_off_button.connect("clicked", self.on_power_off)
        power_off_button.set_margin_top(5)
        grid.attach(power_off_button, 1, 0, 1, 1)

        # Volume Up Button
        volume_up_button = Gtk.Button(label="Volume +")
        volume_up_button.connect("clicked", self.on_volume_up)
        volume_up_button.set_margin_top(5)
        grid.attach(volume_up_button, 0, 1, 1, 1)

        # Volume Down Button
        volume_down_button = Gtk.Button(label="Volume -")
        volume_down_button.connect("clicked", self.on_volume_down)
        volume_down_button.set_margin_top(5)
        grid.attach(volume_down_button, 1, 1, 1, 1)

        # Volume Entry
        self.volume_entry = Gtk.Entry()
        self.volume_entry.set_placeholder_text("Enter Volume (e.g., 45)")
        self.volume_entry.set_margin_top(5)
        grid.attach(self.volume_entry, 0, 2, 2, 1)

        # Set Volume Button
        set_volume_button = Gtk.Button(label="Set Volume")
        set_volume_button.connect("clicked", self.on_set_volume)
        set_volume_button.set_margin_top(5)
        grid.attach(set_volume_button, 0, 3, 2, 1)

        # Mute Button
        mute_button = Gtk.Button(label="Mute")
        mute_button.connect("clicked", self.on_mute)
        mute_button.set_margin_top(5)
        grid.attach(mute_button, 0, 4, 1, 1)

        # Unmute Button
        unmute_button = Gtk.Button(label="Unmute")
        unmute_button.connect("clicked", self.on_unmute)
        unmute_button.set_margin_top(5)
        grid.attach(unmute_button, 1, 4, 1, 1)

    def send_command(self, command):
        try:
            with socket.create_connection((self.avr_ip, self.avr_port), timeout=2) as sock:
                sock.sendall((command + "\r").encode('utf-8'))
        except Exception as e:
            print(f"Error sending command: {e}")

    def on_power_on(self, widget):
        self.send_command("PWON")

    def on_power_off(self, widget):
        self.send_command("PWSTANDBY")

    def on_volume_up(self, widget):
        self.send_command("MVUP")

    def on_volume_down(self, widget):
        self.send_command("MVDOWN")

    def on_set_volume(self, widget):
        volume = self.volume_entry.get_text()
        if volume.isdigit():
            vol_int = int(volume)
            if 0 <= vol_int <= 98:
                self.send_command(f"MV{vol_int:02d}")
            else:
                print("Volume must be between 0 and 98.")
        else:
            print("Please enter a valid numeric volume.")

    def on_mute(self, widget):
        self.send_command("MUON")

    def on_unmute(self, widget):
        self.send_command("MUOFF")

def main():
    win = DenonControl()
    win.connect("destroy", Gtk.main_quit)
    win.show_all()
    Gtk.main()

if __name__ == "__main__":
    main()
