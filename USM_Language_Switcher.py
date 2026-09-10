import os
import sys
import shutil
import ctypes
import winreg
import tkinter as tk
from tkinter import ttk, messagebox, filedialog

REG_KEY_PATH = r"Software\Activision\Ultimate Spider-Man\Settings"

LANGUAGES = [
    {"id": "TR", "reg_val": 0, "name": "Türkçe (Turkish)", "flag": "🇹🇷", "desc": "Tam Türkçe Çeviri + 4K HD Çizgi Roman Fontları"},
    {"id": "EN", "reg_val": 0, "name": "English (İngilizce)", "flag": "🇬🇧", "desc": "Original English Language"},
    {"id": "FR", "reg_val": 1, "name": "Français (Fransızca)", "flag": "🇫🇷", "desc": "Version Française"},
    {"id": "DE", "reg_val": 2, "name": "Deutsch (Almanca)", "flag": "🇩🇪", "desc": "Deutsche Version"},
    {"id": "ES", "reg_val": 3, "name": "Español (İspanyolca)", "flag": "🇪🇸", "desc": "Versión Española"},
    {"id": "IT", "reg_val": 4, "name": "Italiano (İtalyanca)", "flag": "🇮🇹", "desc": "Versione Italiana"},
]

def get_base_dir():
    if getattr(sys, 'frozen', False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))

def read_registry_lang():
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH, 0, winreg.KEY_READ) as key:
            val, _ = winreg.QueryValueEx(key, "Language")
            return val
    except Exception:
        return 0

def write_registry_lang(val):
    try:
        with winreg.CreateKey(winreg.HKEY_CURRENT_USER, REG_KEY_PATH) as key:
            winreg.SetValueEx(key, "Language", 0, winreg.REG_DWORD, int(val))
            return True
    except Exception as e:
        print(f"Registry write error: {e}")
        return False

class LanguageSwitcherApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Ultimate Spider-Man - Dil Değiştirici (Language Switcher)")
        self.root.geometry("660x560")
        self.root.resizable(False, False)
        self.root.configure(bg="#1a1a24")

        self.base_dir = get_base_dir()
        self.game_dir = tk.StringVar(value=self.find_game_dir())
        self.selected_lang = tk.StringVar(value=self.detect_current_lang())

        self.create_widgets()

    def find_game_dir(self):
        cur = self.base_dir
        if os.path.exists(os.path.join(cur, "Ultimate Spider-Man.exe")) or os.path.exists(os.path.join(cur, "USM.exe")):
            return cur

        common_paths = [
            r"E:\Ultimate Spider-Man",
            r"D:\Ultimate Spider-Man",
            r"C:\Program Files (x86)\Activision\Ultimate Spider-Man",
            r"C:\Games\Ultimate Spider-Man",
        ]
        for p in common_paths:
            if os.path.exists(p):
                return p

        return cur

    def detect_current_lang(self):
        gdir = self.game_dir.get() if hasattr(self, 'game_dir') else self.find_game_dir()
        reg_lang = read_registry_lang()
        
        if reg_lang == 0:
            bak1 = os.path.join(gdir, "usm_lte.usm.bak")
            bak2 = os.path.join(gdir, "data", "usm_lte.usm.bak")
            mod_font = os.path.join(gdir, "mods", "damnnoisykids.dds")
            if os.path.exists(bak1) or os.path.exists(bak2) or os.path.exists(mod_font):
                return "TR"
            return "EN"
        
        for l in LANGUAGES:
            if l["id"] != "TR" and l["reg_val"] == reg_lang:
                return l["id"]
        return "EN"

    def browse_game_dir(self):
        d = filedialog.askdirectory(title="Ultimate Spider-Man Oyun Klasörünü Seçin", initialdir=self.game_dir.get())
        if d:
            self.game_dir.set(d)
            self.selected_lang.set(self.detect_current_lang())
            self.update_status()

    def create_widgets(self):
        banner = tk.Frame(self.root, bg="#111118", height=75)
        banner.pack(fill="x", side="top")
        
        lbl_title = tk.Label(banner, text="🕷️ ULTIMATE SPIDER-MAN", font=("Segoe UI", 16, "bold"), fg="#e62429", bg="#111118")
        lbl_title.pack(anchor="w", padx=20, pady=(10, 0))
        
        lbl_subtitle = tk.Label(banner, text="Dil Yöneticisi & 4K Türkçe Yama Sistemi", font=("Segoe UI", 9), fg="#8c8c9e", bg="#111118")
        lbl_subtitle.pack(anchor="w", padx=20, pady=(0, 10))

        dir_frame = tk.Frame(self.root, bg="#1a1a24")
        dir_frame.pack(fill="x", padx=20, pady=12)

        lbl_dir = tk.Label(dir_frame, text="Oyun Klasörü (Game Directory):", font=("Segoe UI", 10, "bold"), fg="#ffffff", bg="#1a1a24")
        lbl_dir.pack(anchor="w")

        dir_box = tk.Frame(dir_frame, bg="#242434", bd=1, relief="solid")
        dir_box.pack(fill="x", pady=5)

        ent_dir = tk.Entry(dir_box, textvariable=self.game_dir, font=("Segoe UI", 9), bg="#242434", fg="#ffffff", relief="flat")
        ent_dir.pack(side="left", fill="x", expand=True, padx=8, pady=6)

        btn_browse = tk.Button(dir_box, text="Gözat...", font=("Segoe UI", 9), bg="#38384f", fg="#ffffff", relief="flat", command=self.browse_game_dir, cursor="hand2")
        btn_browse.pack(side="right", padx=5, pady=4)

        lang_frame = tk.Frame(self.root, bg="#1a1a24")
        lang_frame.pack(fill="x", padx=20, pady=5)

        lbl_select = tk.Label(lang_frame, text="Kullanmak İstediğiniz Dili Seçin:", font=("Segoe UI", 10, "bold"), fg="#ffffff", bg="#1a1a24")
        lbl_select.pack(anchor="w", pady=(0, 6))

        options_box = tk.Frame(lang_frame, bg="#242434", bd=1, relief="solid")
        options_box.pack(fill="x")

        for lang in LANGUAGES:
            row = tk.Frame(options_box, bg="#242434")
            row.pack(fill="x", padx=12, pady=5)

            rb = tk.Radiobutton(
                row,
                text=f"{lang['flag']}  {lang['name']}",
                value=lang["id"],
                variable=self.selected_lang,
                font=("Segoe UI", 10, "bold" if lang["id"] == "TR" else "normal"),
                fg="#ff4444" if lang["id"] == "TR" else "#ffffff",
                bg="#242434",
                activebackground="#242434",
                activeforeground="#ffffff",
                selectcolor="#111118",
                cursor="hand2"
            )
            rb.pack(side="left")

            lbl_desc = tk.Label(row, text=f"- {lang['desc']}", font=("Segoe UI", 8), fg="#7e7e94", bg="#242434")
            lbl_desc.pack(side="left", padx=8)

        self.lbl_status = tk.Label(self.root, text="", font=("Segoe UI", 9, "italic"), fg="#00e676", bg="#1a1a24")
        self.lbl_status.pack(pady=8)
        self.update_status()

        btn_frame = tk.Frame(self.root, bg="#1a1a24")
        btn_frame.pack(fill="x", padx=20, pady=10)

        btn_apply = tk.Button(btn_frame, text="✓ Dili Uygula (Apply Language)", font=("Segoe UI", 11, "bold"), bg="#e62429", fg="#ffffff", relief="flat", padx=15, pady=8, command=self.apply_language, cursor="hand2")
        btn_apply.pack(side="left", fill="x", expand=True, padx=(0, 8))

        btn_launch = tk.Button(btn_frame, text="▶ Oyunu Başlat", font=("Segoe UI", 11, "bold"), bg="#1e88e5", fg="#ffffff", relief="flat", padx=15, pady=8, command=self.launch_game, cursor="hand2")
        btn_launch.pack(side="right", fill="x", expand=True, padx=(8, 0))

    def update_status(self):
        cur = self.detect_current_lang()
        for l in LANGUAGES:
            if l["id"] == cur:
                self.lbl_status.config(text=f"Şu Anki Aktif Dil: {l['flag']} {l['name']}")
                break

    def apply_language(self):
        gdir = self.game_dir.get()
        if not os.path.exists(gdir):
            messagebox.showerror("Hata", f"Belirtilen oyun klasörü bulunamadı:\n{gdir}")
            return

        lang_id = self.selected_lang.get()
        target_lang = next((l for l in LANGUAGES if l["id"] == lang_id), None)
        if not target_lang:
            return

        write_registry_lang(target_lang["reg_val"])

        font_dir = os.path.join(self.base_dir, "fontlar")
        if not os.path.exists(font_dir):
            font_dir = r"d:\openusm\fontlar"
        if not os.path.exists(font_dir):
            font_dir = self.base_dir

        mods_dir = os.path.join(gdir, "mods")
        
        # Targets for usm_lte.usm (both root and data/ folder)
        lte_paths = [
            os.path.join(gdir, "usm_lte.usm"),
            os.path.join(gdir, "data", "usm_lte.usm")
        ]

        if lang_id == "TR":
            os.makedirs(mods_dir, exist_ok=True)

            # 1. Backup original usm_lte.usm files
            for p in lte_paths:
                bak_p = p + ".bak"
                if os.path.exists(p) and not os.path.exists(bak_p):
                    try:
                        shutil.copy2(p, bak_p)
                    except Exception as e:
                        print(f"Backup error for {p}: {e}")

            # 2. Find source usm_ltr.usm
            ltr_src = None
            for candidate in [
                os.path.join(self.base_dir, "usm_ltr.usm"),
                r"d:\openusm\usm_ltr.usm",
                os.path.join(gdir, "usm_ltr.usm"),
                os.path.join(self.base_dir, "usm_lte.usm"),
                r"d:\openusm\usm_lte.usm",
            ]:
                if os.path.exists(candidate):
                    ltr_src = candidate
                    break

            if ltr_src:
                for p in lte_paths:
                    try:
                        os.makedirs(os.path.dirname(p), exist_ok=True)
                        shutil.copy2(ltr_src, p)
                    except Exception as e:
                        print(f"Copy error to {p}: {e}")

            # 3. Copy Turkish 4K HD fonts to mods/
            font_files = [
                "damnnoisykids.dds", "damnnoisykids.fdf",
                "badaboom.dds", "badaboom.fdf",
                "i_upupandaway.dds", "i_upupandaway.fdf"
            ]
            copied_count = 0
            for f in font_files:
                src_f = os.path.join(font_dir, f)
                if not os.path.exists(src_f):
                    src_f = os.path.join(r"d:\openusm\fontlar", f)
                if os.path.exists(src_f):
                    try:
                        shutil.copy2(src_f, os.path.join(mods_dir, f))
                        copied_count += 1
                    except Exception as e:
                        print(f"Font copy error: {f} -> {e}")

            messagebox.showinfo(
                "Başarılı",
                f"🇹🇷 Türkçe Dil Paketi ve 4K Fontlar Başarıyla Uygulandı!\n\n"
                f"- Dil Ayarı: Türkçe (English Base)\n"
                f"- Çeviri Dosyası: data\\usm_lte.usm güncellendi (Yedek: .bak)\n"
                f"- Fontlar: {copied_count} adet 4K HD Türkçe font mods/ klasörüne yüklendi."
            )

        else:
            for p in lte_paths:
                bak_p = p + ".bak"
                if os.path.exists(bak_p):
                    try:
                        shutil.copy2(bak_p, p)
                    except Exception as e:
                        print(f"Restore error for {p}: {e}")

            if os.path.exists(mods_dir):
                for f in ["damnnoisykids.dds", "damnnoisykids.fdf", "badaboom.dds", "badaboom.fdf", "i_upupandaway.dds", "i_upupandaway.fdf"]:
                    mod_f = os.path.join(mods_dir, f)
                    if os.path.exists(mod_f):
                        try:
                            os.remove(mod_f)
                        except Exception:
                            pass

            messagebox.showinfo(
                "Başarılı",
                f"{target_lang['flag']} {target_lang['name']} dili başarıyla uygulandı!\n\n"
                f"- Kayıt Defteri: Language = {target_lang['reg_val']}\n"
                f"- Orijinal İngilizce metin dosyası geri yüklendi."
            )

        self.update_status()

    def launch_game(self):
        gdir = self.game_dir.get()
        for exe_name in ["Ultimate Spider-Man.exe", "USM.exe", "Spider-Man.exe"]:
            exe_path = os.path.join(gdir, exe_name)
            if os.path.exists(exe_path):
                try:
                    os.startfile(exe_path)
                    return
                except Exception:
                    try:
                        ctypes.windll.shell32.ShellExecuteW(None, "open", exe_path, None, gdir, 1)
                        return
                    except Exception as e2:
                        messagebox.showerror("Hata", f"Oyun başlatılamadı:\n{e2}")
                        return
        messagebox.showwarning("Bulunamadı", f"Oyun çalıştırılabilir dosyası (.exe) bulunamadı:\n{gdir}")

if __name__ == "__main__":
    root = tk.Tk()
    app = LanguageSwitcherApp(root)
    root.mainloop()
