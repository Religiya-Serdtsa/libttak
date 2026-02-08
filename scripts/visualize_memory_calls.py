import sys
import subprocess
import json
import networkx as nx
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from matplotlib.gridspec import GridSpec
from matplotlib.widgets import Button, Slider
from tkinter import filedialog, Tk

class UniversalMemoryDebugger:
    def __init__(self, raw_input):
        """
        High-Fidelity Engineering Debugger.
        Uses Grayscale density to visualize memory depth and access heat.
        """
        # 1. Parse and Process Metadata
        self.logs = self._parse_logs(raw_input)
        if not self.logs:
            print("Error: No valid [MEM_TRACK] tokens found.")
            sys.exit(1)

        # 2. Build State Snapshots (Full Temporal Trace)
        self.snapshots = self._generate_snapshots()
        self.total_frames = len(self.snapshots)
        self.current_idx = 0
        self.is_paused = False
        self.playback_speed = 1.0
        self.export_name = "memory_analysis_report.gif"

        # 3. Setup UI: Pure White Background & Grayscale Hierarchy
        plt.rcParams['text.color'] = 'black'
        plt.rcParams['axes.labelcolor'] = 'black'
        self.fig = plt.figure(figsize=(22, 13), facecolor='white')

        # GridSpec for Unified Layout
        gs = GridSpec(4, 2, width_ratios=[1.3, 1], height_ratios=[1, 1, 0.08, 0.08], figure=self.fig)

        self.ax_graph = self.fig.add_subplot(gs[0:2, 0], facecolor='white')
        self.ax_table = self.fig.add_subplot(gs[0, 1], facecolor='white')
        self.ax_inspect = self.fig.add_subplot(gs[1, 1], facecolor='white')
        self.ax_control = self.fig.add_subplot(gs[2:, :], facecolor='white')

        self._init_controls()
        self._start_playback()

    def _parse_logs(self, raw_data):
        """Extracts and cleanses JSON data from raw input stream."""
        extracted = []
        for line in raw_data:
            if "[MEM_TRACK]" in line:
                try:
                    payload = json.loads(line.split("[MEM_TRACK]")[1].strip())
                    extracted.append(payload)
                except (json.JSONDecodeError, IndexError):
                    continue
        return sorted(extracted, key=lambda x: (x.get("ts", 0), extracted.index(x)))

    def _generate_snapshots(self):
        """Reconstructs the global memory state for every discrete event."""
        G = nx.DiGraph()
        snapshots = []
        registry = {}

        for log in self.logs:
            p, e = log.get("ptr"), log.get("event")

            if e == "alloc":
                registry[p] = {
                    "name": log.get("name", p[-6:]),
                    "size": log.get("size", 0),
                    "root": log.get("root", 0),
                    "count": 0,
                    "status": "ALIVE",
                    "owner": "SYSTEM_ROOT" if log.get("root") else "UNASSIGNED"
                }
                G.add_node(p, layer=1)
                G.add_node(registry[p]["owner"], layer=0)
                G.add_edge(registry[p]["owner"], p)

            elif e == "register":
                owner = log.get("owner")
                if p in registry:
                    old_owner = registry[p]["owner"]
                    if G.has_edge(old_owner, p):
                        G.remove_edge(old_owner, p)
                    registry[p]["owner"] = owner
                    if log.get("name"): registry[p]["name"] = log.get("name")
                    G.add_node(owner, layer=0)
                    G.add_edge(owner, p)

            elif e == "access":
                if p in registry:
                    registry[p]["count"] = log.get("count", registry[p].get("count", 0) + 1)

            elif e == "free":
                if p in registry:
                    registry[p]["status"] = "FREED"
                    if G.has_edge(registry[p]["owner"], p):
                        G.remove_edge(registry[p]["owner"], p)

            snapshots.append({
                "graph": G.copy(),
                "registry": json.loads(json.dumps(registry)),
                "log": log
            })
        return snapshots

    def _init_controls(self):
        """Initializes GUI elements with high-contrast labels and file explorer integration."""
        self.ax_control.axis('off')

        # 1. Timeline Slider (Progress Tracking)
        ax_slid = self.fig.add_axes([0.15, 0.08, 0.35, 0.025], facecolor='#f0f0f0')
        self.slider = Slider(ax_slid, 'Timeline [Tick] ', 0, self.total_frames - 1,
                             valinit=0, valfmt='%d', color="#333333")
        self.slider.label.set_fontsize(9)
        self.slider.on_changed(self._jump_to)

        # 2. Playback Speed Slider (Speed Control)
        ax_speed = self.fig.add_axes([0.15, 0.04, 0.35, 0.025], facecolor='#f0f0f0')
        self.speed_slider = Slider(ax_speed, 'Speed [Factor] ', 0.1, 5.0,
                                   valinit=1.0, valfmt='%.1fx', color="#333333")
        self.speed_slider.label.set_fontsize(9)
        self.speed_slider.on_changed(self._update_speed)

        # 3. Play/Pause Button
        ax_pp = self.fig.add_axes([0.55, 0.05, 0.06, 0.04])
        self.btn_pp = Button(ax_pp, 'PAUSE', color='#ffffff', hovercolor='#e0e0e0')
        self.btn_pp.label.set_color("black")
        self.btn_pp.on_clicked(self._toggle_playback)

        # 4. Save/Export via File Explorer
        ax_save_btn = self.fig.add_axes([0.75, 0.05, 0.15, 0.04])
        self.btn_save = Button(ax_save_btn, 'SELECT PATH & EXPORT', color='#ffffff', hovercolor='#e0e0e0')
        self.btn_save.label.set_color("black")
        self.btn_save.on_clicked(self._select_and_save)

    def _select_and_save(self, event):
        """Invokes system file manager for export path selection."""
        root = Tk(); root.withdraw()
        file_path = filedialog.asksaveasfilename(
            defaultextension=".gif", filetypes=[("GIF Animation", "*.gif")],
            initialfile=self.export_name, title="Export Debug Trace"
        )
        root.destroy()
        if file_path:
            self.export_name = file_path
            self._save_report(None)

    def _update_speed(self, val):
        self.playback_speed = val
        self.ani.event_source.interval = int(1000 / self.playback_speed)

    def _toggle_playback(self, event):
        self.is_paused = not self.is_paused
        self.btn_pp.label.set_text('PLAY' if self.is_paused else 'PAUSE')

    def _jump_to(self, val):
        self.current_idx = int(val); self._render_frame(self.current_idx)

    def _save_report(self, event):
        self.ani.save(self.export_name, writer='pillow', fps=max(1, int(self.playback_speed)))
        print(f"Export Complete: {self.export_name}")

    def _render_frame(self, idx):
        """Unified rendering using grayscale depth and high-contrast labels."""
        self.ax_graph.clear(); self.ax_table.clear(); self.ax_inspect.clear()
        snap = self.snapshots[idx]; G, reg, log = snap["graph"], snap["registry"], snap["log"]

        try:
            from networkx.drawing.nx_agraph import graphviz_layout
            pos = graphviz_layout(G, prog='dot')
        except ImportError:
            pos = nx.spring_layout(G, k=0.5)

        # Node Visualization: Grayscale Depth Mapping
        node_colors = []
        for n in G.nodes():
            if n in reg:
                if reg[n]["status"] == "FREED":
                    node_colors.append("#e0e0e0") # Very light grey for dead blocks
                else:
                    # Access density (Heat) mapped to grayscale (Darker = More accessed)
                    hits = reg[n].get("count", 0)
                    intensity = 1.0 - min(0.7, hits / 15.0)
                    node_colors.append((intensity, intensity, intensity))
            else:
                node_colors.append("#808080") # Mid-grey for Owner nodes

        nx.draw(G, pos, ax=self.ax_graph, with_labels=True,
                labels={n: f"{reg[n]['name']}\n{reg[n]['size']}B" if n in reg else n[:8] for n in G.nodes()},
                node_color=node_colors, node_size=2400, font_size=6, font_color="black",
                edge_color="black", linewidths=1.5)

        self.ax_graph.set_title(f"TRACE HIERARCHY | TICK {idx+1}/{self.total_frames}", loc='left')

        # 2. Table: Filtering and Detail
        self.ax_table.axis('off')
        headers = ["Name", "Address", "Size", "Hits", "Status"]
        active_rows = [[d['name'], p[-10:], f"{d['size']}B", d['count'], "LIVE"]
                       for p, d in reg.items() if d['status'] == "ALIVE"]
        active_rows.sort(key=lambda x: x[3], reverse=True)

        tbl = self.ax_table.table(cellText=[headers] + active_rows[:20], loc='center', cellLoc='center')
        tbl.auto_set_font_size(False); tbl.set_fontsize(8); tbl.scale(1, 1.6)
        for k, cell in tbl.get_celld().items():
            cell.set_edgecolor('black')
            if k[0] == 0:
                cell.set_text_props(weight='bold', color='white'); cell.set_facecolor('black')
            else: cell.set_facecolor('white')

        self.ax_inspect.axis('off')
        self.ax_inspect.text(0.05, 0.5, f"EVENT LOG:\n{json.dumps(log, indent=2)}",
                             family='monospace', color="black", fontsize=9, va='center')

    def _start_playback(self):
        def update_wrapper(i):
            if not self.is_paused:
                self.current_idx = (self.current_idx + 1) % self.total_frames
                self.slider.set_val(self.current_idx)
            return []
        self.ani = FuncAnimation(self.fig, update_wrapper, frames=self.total_frames, interval=1000)
        plt.show()

if __name__ == "__main__":
    from subprocess import PIPE
    # Your get_input_logs logic here
    def get_input_logs(argv):
        lines = []
        if len(argv) > 1:
            if argv[1].endswith(".log"):
                with open(argv[1], "r") as f: lines = f.readlines()
            else:
                proc = subprocess.Popen(argv[1:], stderr=PIPE, text=True)
                for line in proc.stderr: lines.append(line)
                proc.wait()
        return lines

    UniversalMemoryDebugger(get_input_logs(sys.argv))
