import logging
import threading

class Worker:
    def __init__(self, metadata):
        self.metadata=metadata
        self.stop_event=threading.Event()

    def start(self):
        threading.Thread(target=self.maintenance,name='ds-reconcile',daemon=True).start()
        for index in range(self.metadata.c.repair_workers):
            threading.Thread(target=self.run,name='ds-task-'+str(index),daemon=True).start()

    def maintenance(self):
        while not self.stop_event.is_set():
            try:
                self.metadata.maintain()
            except Exception:
                logging.exception('metadata maintenance failed')
            self.stop_event.wait(2)

    def run(self):
        while not self.stop_event.is_set():
            try:
                self.metadata.run_once()
            except Exception:
                logging.exception('background task failed')
            self.stop_event.wait(0.2)
