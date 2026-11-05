import threading
import time
class Worker:
    def __init__(self, metadata, interval=0.2):
        self.metadata, self.interval = metadata, interval
        self.stop_event = threading.Event()
    def start(self):
        threading.Thread(target=self.run, name='ds-task-worker', daemon=True).start()
    def run(self):
        while not self.stop_event.is_set():
            try:
                did = self.metadata.run_once()
            except Exception:
                did = False
            if not did:
                self.stop_event.wait(self.interval)
