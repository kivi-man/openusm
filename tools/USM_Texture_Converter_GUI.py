"""
Ultimate Spider-Man - DDS Doku Dönüştürücü (GUI)
Şeffaf (Alpha) ve Düz PNG'leri Ultimate Spider-Man ve OpenUSM mod formatına (DDS DXT1/DXT5) dönüştürür.
"""

import os
import sys
import struct
import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import numpy as np
from PIL import Image

# ----------------- DDS & SIKIŞTIRMA MOTORU -----------------

def is_power_of_two(n):
    return (n > 0) and (n & (n - 1) == 0)

def next_power_of_two(n):
    if n <= 0:
        return 1
    p = 1
    while p < n:
        p <<= 1
    return p

def has_transparency(im):
    if im.mode not in ('RGBA', 'LA') and 'transparency' not in im.info:
        return False
    if im.mode != 'RGBA':
        im = im.convert('RGBA')
    extrema = im.getextrema()
    if len(extrema) >= 4:
        alpha_min, alpha_max = extrema[3]
        return alpha_min < 255
    return False

def rgb_to_565(r, g, b):
    return ((int(r) >> 3) << 11) | ((int(g) >> 2) << 5) | (int(b) >> 3)

def encode_dxt1_block_np(block_rgb):
    # block_rgb: (4, 4, 3) uint8
    pixels = block_rgb.reshape(16, 3)
    min_rgb = pixels.min(axis=0)
    max_rgb = pixels.max(axis=0)
    
    c0 = rgb_to_565(max_rgb[0], max_rgb[1], max_rgb[2])
    c1 = rgb_to_565(min_rgb[0], min_rgb[1], min_rgb[2])
    if c0 < c1:
        c0, c1 = c1, c0
        
    r0 = (c0 >> 11) << 3; g0 = ((c0 >> 5) & 0x3F) << 2; b0 = (c0 & 0x1F) << 3
    r1 = (c1 >> 11) << 3; g1 = ((c1 >> 5) & 0x3F) << 2; b1 = (c1 & 0x1F) << 3
    
    colors = np.array([
        [r0, g0, b0],
        [r1, g1, b1],
        [(2*r0 + r1)//3, (2*g0 + g1)//3, (2*b0 + b1)//3],
        [(r0 + 2*r1)//3, (g0 + 2*g1)//3, (b0 + 2*b1)//3]
    ], dtype=np.int32)
    
    # Vectorized distance calculation
    # pixels: (16, 3), colors: (4, 3) -> diff: (16, 4, 3)
    diff = pixels[:, np.newaxis, :] - colors[np.newaxis, :, :]
    dist = np.sum(diff**2, axis=2) # (16, 4)
    best_indices = np.argmin(dist, axis=1) # (16,)
    
    indices_val = 0
    for i, idx in enumerate(best_indices):
        indices_val |= (int(idx) << (2 * i))
        
    return struct.pack('<HHI', c0, c1, indices_val)

def encode_dxt5_alpha_block_np(block_alpha):
    # block_alpha: (4, 4) uint8
    alphas = block_alpha.reshape(16)
    a0 = int(alphas.max())
    a1 = int(alphas.min())
    
    if a0 == a1:
        return struct.pack('<BB6s', a0, a1, b'\x00'*6)
        
    table = np.array([
        a0,
        a1,
        (6*a0 + 1*a1)//7,
        (5*a0 + 2*a1)//7,
        (4*a0 + 3*a1)//7,
        (3*a0 + 4*a1)//7,
        (2*a0 + 5*a1)//7,
        (1*a0 + 6*a1)//7
    ], dtype=np.int32)
    
    diff = np.abs(alphas[:, np.newaxis] - table[np.newaxis, :])
    best_indices = np.argmin(diff, axis=1)
    
    indices_val = 0
    for i, idx in enumerate(best_indices):
        indices_val |= (int(idx) << (3 * i))
        
    idx_bytes = struct.pack('<Q', indices_val)[:6]
    return struct.pack('<BB', a0, a1) + idx_bytes

def compress_surface(im, fourcc_str):
    # im is PIL Image (RGBA)
    w, h = im.size
    arr = np.array(im, dtype=np.uint8)
    
    # Pad to multiple of 4 if needed
    pad_w = ((w + 3) // 4) * 4
    pad_h = ((h + 3) // 4) * 4
    if pad_w != w or pad_h != h:
        new_arr = np.zeros((pad_h, pad_w, 4), dtype=np.uint8)
        new_arr[:h, :w, :] = arr
        arr = new_arr
        
    blocks_x = pad_w // 4
    blocks_y = pad_h // 4
    
    output_bytes = bytearray()
    
    for by in range(blocks_y):
        for bx in range(blocks_x):
            sub_block = arr[by*4:(by+1)*4, bx*4:(bx+1)*4]
            if fourcc_str == b'DXT5':
                alpha_block = encode_dxt5_alpha_block_np(sub_block[:, :, 3])
                rgb_block = encode_dxt1_block_np(sub_block[:, :, :3])
                output_bytes.extend(alpha_block)
                output_bytes.extend(rgb_block)
            else: # DXT1
                rgb_block = encode_dxt1_block_np(sub_block[:, :, :3])
                output_bytes.extend(rgb_block)
                
    return bytes(output_bytes)

def generate_dds_header(width, height, fourcc_str, mip_count=1):
    flags = 0x1 | 0x2 | 0x4 | 0x1000  # CAPS | HEIGHT | WIDTH | PIXELFORMAT
    if mip_count > 1:
        flags |= 0x20000  # MIPMAPCOUNT
    if fourcc_str:
        flags |= 0x80000  # LINEARSIZE
        pitch_or_linear = max(1, ((width + 3) // 4)) * (8 if fourcc_str == b'DXT1' else 16) * ((height + 3) // 4)
    else:
        flags |= 0x8  # PITCH
        pitch_or_linear = width * 4
        
    caps1 = 0x1000  # TEXTURE
    if mip_count > 1:
        caps1 |= 0x400000 | 0x8  # COMPLEX | MIPMAP
        
    if fourcc_str:
        pf_flags = 0x4  # FOURCC
        pf_fourcc = fourcc_str
        pf_rgb_bits = 0
        pf_r_mask = pf_g_mask = pf_b_mask = pf_a_mask = 0
    else:
        pf_flags = 0x41  # ALPHAPIXELS | RGB
        pf_fourcc = b'\x00\x00\x00\x00'
        pf_rgb_bits = 32
        pf_r_mask = 0x00FF0000
        pf_g_mask = 0x0000FF00
        pf_b_mask = 0x000000FF
        pf_a_mask = 0xFF000000
        
    pf_struct = struct.pack('<II4sIIIII', 32, pf_flags, pf_fourcc, pf_rgb_bits, pf_r_mask, pf_g_mask, pf_b_mask, pf_a_mask)
    
    header = struct.pack(
        '<4sIIIIIII44s32sIIIII',
        b'DDS ', 124, flags, height, width, pitch_or_linear, 0,
        mip_count, b'\x00'*44, pf_struct, caps1, 0, 0, 0, 0
    )
    return header

def convert_png_to_dds(input_path, output_path, mode='auto', gen_mipmaps=True, resize_pow2=True):
    img = Image.open(input_path)
    if img.mode != 'RGBA':
        img = img.convert('RGBA')
        
    w, h = img.size
    if resize_pow2 and (not is_power_of_two(w) or not is_power_of_two(h)):
        target_w = next_power_of_two(w)
        target_h = next_power_of_two(h)
        img = img.resize((target_w, target_h), Image.Resampling.LANCZOS)
        w, h = img.size
        
    # Format belirleme
    if mode == 'auto':
        has_alpha = has_transparency(img)
        fourcc = b'DXT5' if has_alpha else b'DXT1'
    elif mode == 'dxt1':
        fourcc = b'DXT1'
    elif mode == 'dxt5':
        fourcc = b'DXT5'
    else:
        fourcc = None # Uncompressed
        
    # Mipmap zinciri
    mip_images = [img]
    if gen_mipmaps:
        cur_w, cur_h = w, h
        while cur_w > 4 or cur_h > 4:
            cur_w = max(1, cur_w // 2)
            cur_h = max(1, cur_h // 2)
            mip_images.append(img.resize((cur_w, cur_h), Image.Resampling.LANCZOS))
            
    header = generate_dds_header(w, h, fourcc, len(mip_images))
    
    with open(output_path, 'wb') as out_f:
        out_f.write(header)
        for mip in mip_images:
            if fourcc in (b'DXT1', b'DXT5'):
                payload = compress_surface(mip, fourcc)
                out_f.write(payload)
            else:
                # Uncompressed ARGB8888
                arr = np.array(mip, dtype=np.uint8) # RGBA
                # Convert RGBA to BGRA for Direct3D 32-bit
                bgra = np.zeros_like(arr)
                bgra[:, :, 0] = arr[:, :, 2] # B
                bgra[:, :, 1] = arr[:, :, 1] # G
                bgra[:, :, 2] = arr[:, :, 0] # R
                bgra[:, :, 3] = arr[:, :, 3] # A
                out_f.write(bgra.tobytes())
                
    fmt_name = fourcc.decode('ascii') if fourcc else 'ARGB8888'
    return fmt_name, w, h, len(mip_images)


# ----------------- MODERN TKINTER GUI -----------------

class TextureConverterApp:
    def __init__(self, root):
        self.root = root
        self.root.title("Ultimate Spider-Man - DDS Doku Dönüştürücü")
        self.root.geometry("780x560")
        self.root.minsize(680, 480)
        
        self.files_to_convert = []
        self.output_dir = "D:/Ultimate Spider-Man/mods"
        if not os.path.exists(self.output_dir):
            self.output_dir = os.getcwd()
            
        self.setup_ui()
        
    def setup_ui(self):
        style = ttk.Style()
        style.theme_use('clam')
        
        # Üst Bilgi Paneli
        header_frame = tk.Frame(self.root, bg="#1E1E2E", padx=16, pady=12)
        header_frame.pack(fill=tk.X)
        
        title_lbl = tk.Label(header_frame, text="🕷️ Ultimate Spider-Man - HD Texture Mod Converter", 
                             font=("Segoe UI", 14, "bold"), fg="#E06C75", bg="#1E1E2E")
        title_lbl.pack(anchor="w")
        
        sub_lbl = tk.Label(header_frame, text="PNG dokularını otomatik analiz eder; şeffafları DXT5, düz olanları DXT1 olarak DirectDraw Surface (.dds) yapar.",
                           font=("Segoe UI", 9), fg="#ABB2BF", bg="#1E1E2E")
        sub_lbl.pack(anchor="w", pady=(2, 0))
        
        # Ana İçerik Çerçevesi
        main_frame = tk.Frame(self.root, padx=16, pady=12)
        main_frame.pack(fill=tk.BOTH, expand=True)
        
        # Dosya Ekleme Butonları
        btn_frame = tk.Frame(main_frame)
        btn_frame.pack(fill=tk.X, pady=(0, 8))
        
        add_file_btn = tk.Button(btn_frame, text="➕ PNG Dosyası Seç...", bg="#61AFEF", fg="white", 
                                 font=("Segoe UI", 9, "bold"), relief=tk.FLAT, padx=12, pady=5,
                                 command=self.add_files)
        add_file_btn.pack(side=tk.LEFT, padx=(0, 6))
        
        add_dir_btn = tk.Button(btn_frame, text="📁 Klasör Seç (Toplu)...", bg="#98C379", fg="white", 
                                font=("Segoe UI", 9, "bold"), relief=tk.FLAT, padx=12, pady=5,
                                command=self.add_directory)
        add_dir_btn.pack(side=tk.LEFT, padx=(0, 6))
        
        clear_btn = tk.Button(btn_frame, text="🗑️ Listeyi Temizle", bg="#E5C07B", fg="black", 
                              font=("Segoe UI", 9), relief=tk.FLAT, padx=10, pady=5,
                              command=self.clear_list)
        clear_btn.pack(side=tk.LEFT)
        
        # Dosya Listesi (Treeview)
        tree_frame = tk.Frame(main_frame)
        tree_frame.pack(fill=tk.BOTH, expand=True, pady=(0, 8))
        
        columns = ("name", "size", "alpha_status")
        self.tree = ttk.Treeview(tree_frame, columns=columns, show="headings", selectmode="extended")
        self.tree.heading("name", text="Dosya Yolu")
        self.tree.heading("size", text="Boyut (px)")
        self.tree.heading("alpha_status", text="Şeffaflık Durumu")
        
        self.tree.column("name", width=420)
        self.tree.column("size", width=120, anchor="center")
        self.tree.column("alpha_status", width=140, anchor="center")
        
        scroll = ttk.Scrollbar(tree_frame, orient="vertical", command=self.tree.yview)
        self.tree.configure(yscrollcommand=scroll.set)
        
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scroll.pack(side=tk.RIGHT, fill=tk.Y)
        
        # Ayarlar Paneli
        opts_frame = tk.LabelFrame(main_frame, text="Dönüştürme Ayarları", font=("Segoe UI", 9, "bold"), padx=10, pady=8)
        opts_frame.pack(fill=tk.X, pady=(0, 8))
        
        self.format_var = tk.StringVar(value="auto")
        fmt_lbl = tk.Label(opts_frame, text="Format Modu:", font=("Segoe UI", 9))
        fmt_lbl.grid(row=0, column=0, sticky="w", padx=(0, 8))
        
        fmt_combo = ttk.Combobox(opts_frame, textvariable=self.format_var, state="readonly", width=32)
        fmt_combo['values'] = ("auto (Otomatik: Şeffaf -> DXT5, Düz -> DXT1)", 
                               "dxt1 (Zorla DXT1 - Saydamsız)", 
                               "dxt5 (Zorla DXT5 - Şeffaf/Alpha Destekli)", 
                               "uncompressed (32-bit ARGB Ham)")
        fmt_combo.current(0)
        fmt_combo.grid(row=0, column=1, sticky="w", padx=(0, 16))
        
        self.mipmap_var = tk.BooleanVar(value=True)
        mip_chk = tk.Checkbutton(opts_frame, text="MipMap Üret (Önerilir)", variable=self.mipmap_var, font=("Segoe UI", 9))
        mip_chk.grid(row=0, column=2, sticky="w", padx=(0, 16))
        
        self.pow2_var = tk.BooleanVar(value=True)
        pow2_chk = tk.Checkbutton(opts_frame, text="Power of 2 Boyut Doğrula (2^n)", variable=self.pow2_var, font=("Segoe UI", 9))
        pow2_chk.grid(row=0, column=3, sticky="w")
        
        # Çıktı Dizini
        out_frame = tk.Frame(main_frame)
        out_frame.pack(fill=tk.X, pady=(0, 8))
        
        out_lbl = tk.Label(out_frame, text="Hedef Klasör:", font=("Segoe UI", 9, "bold"))
        out_lbl.pack(side=tk.LEFT, padx=(0, 6))
        
        self.out_entry = tk.Entry(out_frame, font=("Segoe UI", 9))
        self.out_entry.insert(0, self.output_dir)
        self.out_entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 6))
        
        browse_out_btn = tk.Button(out_frame, text="Değiştir...", command=self.browse_output, font=("Segoe UI", 9))
        browse_out_btn.pack(side=tk.LEFT, padx=(0, 6))
        
        open_out_btn = tk.Button(out_frame, text="📂 Klasörü Aç", command=self.open_output_dir, font=("Segoe UI", 9))
        open_out_btn.pack(side=tk.LEFT)
        
        # İlerleme Çubuğu ve Başlat Butonu
        action_frame = tk.Frame(main_frame)
        action_frame.pack(fill=tk.X, pady=(4, 0))
        
        self.progress = ttk.Progressbar(action_frame, orient="horizontal", mode="determinate")
        self.progress.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 12))
        
        self.convert_btn = tk.Button(action_frame, text="🚀 DDS DOKULARINA DÖNÜŞTÜR", bg="#E06C75", fg="white",
                                     font=("Segoe UI", 10, "bold"), relief=tk.FLAT, padx=16, pady=8,
                                     command=self.start_conversion_thread)
        self.convert_btn.pack(side=tk.RIGHT)
        
        # Durum Çubuğu
        self.status_lbl = tk.Label(self.root, text="Hazır. PNG dosyalarını seçin veya sürükleyin.", bd=1, relief=tk.SUNKEN, anchor="w", font=("Segoe UI", 8))
        self.status_lbl.pack(side=tk.BOTTOM, fill=tk.X)

    def add_files(self):
        paths = filedialog.askopenfilenames(
            title="PNG Doku Dosyalarını Seçin",
            filetypes=[("PNG Resimleri", "*.png"), ("Tüm Resimler", "*.png;*.jpg;*.jpeg;*.bmp;*.tga")]
        )
        if paths:
            for p in paths:
                self.insert_file(p)
                
    def add_directory(self):
        d = filedialog.askdirectory(title="PNG Dosyalarını İçeren Klasörü Seçin")
        if d:
            for root, dirs, files in os.walk(d):
                for f in files:
                    if f.lower().endswith(('.png', '.tga', '.bmp', '.jpg', '.jpeg')):
                        self.insert_file(os.path.join(root, f))
                        
    def insert_file(self, path):
        if path not in self.files_to_convert:
            self.files_to_convert.append(path)
            try:
                with Image.open(path) as im:
                    sz_str = f"{im.width} x {im.height}"
                    alpha_str = "Şeffaf (DXT5)" if has_transparency(im) else "Düz Opaque (DXT1)"
            except Exception:
                sz_str = "Bilinmiyor"
                alpha_str = "Bilinmiyor"
                
            self.tree.insert("", tk.END, values=(path, sz_str, alpha_str))
            self.status_lbl.config(text=f"Listede {len(self.files_to_convert)} dosya var.")

    def clear_list(self):
        self.files_to_convert.clear()
        for item in self.tree.get_children():
            self.tree.delete(item)
        self.status_lbl.config(text="Liste temizlendi.")
        self.progress['value'] = 0

    def browse_output(self):
        d = filedialog.askdirectory(title="DDS Çıktı Klasörünü Seçin", initialdir=self.output_dir)
        if d:
            self.output_dir = d
            self.out_entry.delete(0, tk.END)
            self.out_entry.insert(0, d)

    def open_output_dir(self):
        target = self.out_entry.get().strip()
        if os.path.exists(target):
            os.startfile(target)
        else:
            messagebox.showwarning("Klasör Bulunamadı", f"Klasör mevcut değil:\n{target}")

    def start_conversion_thread(self):
        if not self.files_to_convert:
            messagebox.showwarning("Dosya Yok", "Lütfen önce dönüştürülecek PNG dosyalarını seçin!")
            return
            
        self.convert_btn.config(state=tk.DISABLED, text="Dönüştürülüyor...")
        t = threading.Thread(target=self.run_conversion)
        t.daemon = True
        t.start()

    def run_conversion(self):
        out_dir = self.out_entry.get().strip()
        os.makedirs(out_dir, exist_ok=True)
        
        mode_val = self.format_var.get().split()[0]
        gen_mip = self.mipmap_var.get()
        pow2 = self.pow2_var.get()
        
        total = len(self.files_to_convert)
        self.progress['maximum'] = total
        
        success = 0
        for i, in_path in enumerate(self.files_to_convert):
            base_name = os.path.splitext(os.path.basename(in_path))[0]
            out_path = os.path.join(out_dir, f"{base_name}.dds")
            
            try:
                fmt, w, h, mips = convert_png_to_dds(in_path, out_path, mode=mode_val, gen_mipmaps=gen_mip, resize_pow2=pow2)
                success += 1
                self.status_lbl.config(text=f"[{i+1}/{total}] {base_name}.dds -> {fmt} ({w}x{h}, {mips} mips) hazır.")
            except Exception as e:
                self.status_lbl.config(text=f"[{i+1}/{total}] HATA: {base_name} ({e})")
                
            self.progress['value'] = i + 1
            self.root.update_idletasks()
            
        self.convert_btn.config(state=tk.NORMAL, text="🚀 DDS DOKULARINA DÖNÜŞTÜR")
        messagebox.showinfo("Tamamlandı", f"İşlem Tamamlandı!\n\nToplam {success} / {total} doku başarıyla DDS formatına dönüştürüldü ve '{out_dir}' klasörüne kaydedildi.")
        self.status_lbl.config(text=f"Dönüştürme tamamlandı: {success}/{total} başarılı.")

def main():
    root = tk.Tk()
    app = TextureConverterApp(root)
    root.mainloop()

if __name__ == '__main__':
    main()
