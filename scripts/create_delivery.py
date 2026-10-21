"""Rebuild the report, portfolio article and narrated-ready demo from recorded evidence.
Uses project-local media packages when available. Never invents performance samples.
"""
from pathlib import Path
import csv, json, math, statistics, subprocess, sys, textwrap, argparse
from functools import lru_cache

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'build/media-tools'))
from PIL import Image, ImageDraw, ImageFont, ImageOps
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor
from reportlab.lib.styles import ParagraphStyle
from reportlab.platypus import Paragraph
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.lib.utils import ImageReader

OUT = ROOT / 'output'; IMG = OUT / 'images'
NAVY='#0b1528'; INK='#18283e'; MUTED='#52647a'; CYAN='#008ca5'; BLUE='#2999eb'; ORANGE='#e58a24'; GREEN='#16846f'
FONT = Path('C:/Windows/Fonts')
for name, file in [('Body','segoeui.ttf'), ('Bold','seguisb.ttf'), ('Mono','consola.ttf')]:
    pdfmetrics.registerFont(TTFont(name, str(FONT/file)))
pdfmetrics.registerFontFamily('Body', normal='Body', bold='Bold', italic='Body', boldItalic='Bold')
S = json.loads((OUT/'evidence/summary.json').read_text())
rows = list(csv.DictReader(l for l in (OUT/'evidence/benchmark-session.csv').read_text().splitlines() if not l.startswith('#')))

def font(size, bold=False): return ImageFont.truetype(str(FONT/('seguisb.ttf' if bold else 'segoeui.ttf')),size)
def save_pngs():
    for f in IMG.glob('*.bmp'):
        Image.open(f).convert('RGB').save(f.with_suffix('.png'))

def diagram():
    im=Image.new('RGB',(1600,670),'#eef4f8');d=ImageDraw.Draw(im)
    d.text((55,32),'One occupied voxel. One output centroid.',font=font(42,True),fill=NAVY)
    d.text((55,94),'A 2D slice illustrates the same rule used along X, Y and Z.',font=font(27),fill=MUTED)
    points=[(100,180),(170,230),(230,160),(270,290),(410,210),(465,300),(620,180),(670,245),(760,310)]
    for ox in [80,920]:
        for i in range(5):d.line((ox+i*140,170,ox+i*140,590),fill='#aabccb',width=2)
        for j in range(4):d.line((ox,170+j*140,ox+560,170+j*140),fill='#aabccb',width=2)
    # Deliberately illustrative coordinates, not measured scan data.
    original=[(120,220),(170,250),(180,200),(250,390),(290,430),(460,260),(470,230),(480,280)]
    for x,y in original:d.ellipse((x-9,y-9,x+9,y+9),fill=BLUE)
    groups=[[(120,220),(170,250),(180,200)],[(250,390),(290,430)],[(460,260),(470,230),(480,280)]]
    for group in groups:
        x=sum(p[0] for p in group)/len(group)+840;y=sum(p[1] for p in group)/len(group)
        d.ellipse((x-12,y-12,x+12,y+12),fill=ORANGE)
    d.text((80,610),'INPUT: samples grouped by voxel',font=font(26,True),fill=BLUE)
    d.text((920,610),'OUTPUT: one representative per group',font=font(26,True),fill=ORANGE)
    d.line((695,355,860,355),fill=CYAN,width=7);d.polygon([(860,355),(835,340),(835,370)],fill=CYAN)
    im.save(IMG/'voxel-concept.png')

def pipeline():
    im=Image.new('RGB',(1600,700),'#eef4f8');d=ImageDraw.Draw(im)
    d.text((50,28),'A reference-first processing engine',font=font(43,True),fill=NAVY)
    boxes=[(60,160,340,285,'PCD frame','Validate XYZ'),(440,100,820,240,'CPU reference','Ordered voxel map'),
           (440,310,820,450,'CUDA backend','Packed keys + CUB'),(940,210,1270,360,'Centroids','Owned output'),(1320,210,1550,360,'Viewer','Paired panels')]
    for x,y,r,b,a,c in boxes:
        d.rounded_rectangle((x,y,r,b),16,fill='white',outline='#b8cbd9',width=2)
        d.text((x+18,y+26),a,font=font(27,True),fill=CYAN if 'CUDA' in a else NAVY)
        d.text((x+18,y+72),c,font=font(21),fill=MUTED)
    for p in [[(340,215),(390,215),(390,170),(440,170)],[(340,215),(390,215),(390,380),(440,380)],[(820,170),(875,170),(875,275),(940,275)],[(820,380),(875,380),(875,290),(940,290)],[(1270,280),(1320,280)]]:
        d.line(p,fill=CYAN,width=5)
    d.rounded_rectangle((60,530,1550,640),12,fill=NAVY)
    d.text((90,551),'Long-lived worker: I/O + processing + benchmark jobs',font=font(31,True),fill='white')
    d.text((90,597),'UI thread: events + drawing only | Futures carry results and errors | RAII owns resources',font=font(23),fill='#acdbe4')
    im.save(IMG/'architecture.png')

def benchmark_chart():
    im=Image.new('RGB',(1600,780),'#eef4f8');d=ImageDraw.Draw(im)
    d.text((60,32),'Measured processing latency',font=font(46,True),fill=NAVY)
    d.text((60,97),'30 frames x 30 repetitions | 900 paired samples | voxel size 0.1',font=font(27),fill=MUTED)
    for y,name,value,col in [(210,'CPU',S['cpu_median_ms'],BLUE),(390,'CUDA',S['cuda_median_ms'],ORANGE)]:
        d.text((65,y),name,font=font(34,True),fill=col)
        d.rounded_rectangle((250,y,250+value/6*1060,y+86),10,fill=col)
        d.text((280+value/6*1060,y+20),f'{value:.3f} ms',font=font(32,True),fill=NAVY)
    d.text((65,565),f"{S['ratio_medians']:.2f}x observed ratio of medians",font=font(43,True),fill=GREEN)
    d.text((65,630),'Full completed backend call; file I/O and rendering excluded.',font=font(27),fill=MUTED)
    d.text((65,679),'One WSL session on Ryzen 7 7735HS + RTX 4050 Laptop. Not a universal speedup.',font=font(23),fill=MUTED)
    im.save(IMG/'benchmark-summary.png')

class PDF:
    W,H=595.276,841.89
    def __init__(self,path,title):
        self.c=canvas.Canvas(str(path),pagesize=(self.W,self.H));self.c.setTitle(title);self.c.setAuthor('cloudForge project');self.n=0
    def page(self,kicker,title,sub=''):
        if self.n:self.c.showPage()
        self.n+=1;self.c.setFillColor(HexColor('#f8fafc'));self.c.rect(0,0,self.W,self.H,fill=1,stroke=0)
        self.c.setFillColor(HexColor(CYAN));self.c.rect(0,self.H-8,self.W,8,fill=1,stroke=0)
        self.t(42,29,'CLOUDFORGE / '+kicker,9,CYAN,True)
        title_size=min(27,511/pdfmetrics.stringWidth(title,'Bold',27)*27)
        self.t(42,62,title,title_size,NAVY,True)
        if sub:self.p(sub,103,size=10,color=MUTED)
        self.c.setStrokeColor(HexColor('#d5e0e9'));self.c.line(42,45,self.W-42,45)
        self.t(42,self.H-34,'Engineering portfolio | 21 October 2026',8,MUTED)
        self.t(self.W-65,self.H-34,str(self.n).zfill(2),8,MUTED)
    def t(self,x,y,text,size=11,color=INK,bold=False):
        self.c.setFillColor(HexColor(color));self.c.setFont('Bold' if bold else 'Body',size);self.c.drawString(x,self.H-y-size,text)
    def p(self,text,y,x=42,w=511,size=11,color=INK):
        style=ParagraphStyle('body',fontName='Body',fontSize=size,leading=size*1.48,textColor=HexColor(color))
        obj=Paragraph(text,style);_,h=obj.wrap(w,1000)
        if y+h>787:raise ValueError(f'Overflow on page {self.n}: {y+h}: {text[:60]}')
        obj.drawOn(self.c,x,self.H-y-h);return y+h
    def h(self,text,y):self.t(42,y,text,14,NAVY,True);return y+26
    def image(self,path,y,h,w=511,x=42):
        im=Image.open(path);iw,ih=im.size;scale=min(w/iw,h/ih);dw,dh=iw*scale,ih*scale
        self.c.drawImage(ImageReader(im),x+(w-dw)/2,self.H-y-dh,width=dw,height=dh,mask='auto');return y+dh
    def caption(self,text,y):return self.p(text,y,size=8.5,color=MUTED)
    def table(self,headers,rows,y,widths=None):
        widths=widths or [511/len(headers)]*len(headers)
        for ri,row in enumerate([headers]+rows):
            cells=[];rh=0
            for txt,w in zip(row,widths):
                p=Paragraph(str(txt),ParagraphStyle('cell',fontName='Bold' if ri==0 else 'Body',fontSize=9,leading=13,textColor=HexColor('#ffffff' if ri==0 else INK)))
                _,ph=p.wrap(w-18,1000);cells.append((p,ph));rh=max(rh,ph+16)
            self.c.setFillColor(HexColor(NAVY if ri==0 else '#eaf1f6' if ri%2 else '#ffffff'));self.c.rect(42,self.H-y-rh,511,rh,fill=1,stroke=0)
            xx=42
            for (p,ph),w in zip(cells,widths):p.drawOn(self.c,xx+9,self.H-y-8-ph);xx+=w
            y+=rh
        return y
    def code(self,lines,y):
        h=16+13*len(lines);self.c.setFillColor(HexColor(NAVY));self.c.roundRect(42,self.H-y-h,511,h,7,fill=1,stroke=0)
        self.c.setFillColor(HexColor('#d5edf4'));self.c.setFont('Mono',8)
        for i,line in enumerate(lines):self.c.drawString(53,self.H-y-18-i*13,line)
        return y+h
    def save(self):self.c.save()

def reports():
    p=PDF(OUT/'pdf/cloudForge-technical-report.pdf','cloudForge - Engineering report')
    p.page('ENGINEERING REPORT','Fewer points. Same 3D scene.','A C++20 point-cloud video engine with an optional CUDA backend and measured correctness.')
    y=p.image(IMG/'cuda-playback.png',145,300)
    p.caption('Actual app capture: recorded Oxford Spires frames, original at left and CUDA centroids at right. Capture is from WSL; a separate native Windows CPU build was also tested.',y+8)
    p.table(['MEASURED SESSION','RESULT'],[
        ['First frame, voxel 0.3','55,518 input points -> 9,943 centroids; 82.09% reduction'],
        ['900 paired timing samples',f"CPU median {S['cpu_median_ms']:.3f} ms; CUDA median {S['cuda_median_ms']:.3f} ms"],
        ['Ratio of pooled medians',f"{S['ratio_medians']:.2f}x observed in this WSL session"],
        ['Validation','35 native Windows CPU tests; 41 WSL CUDA-build tests passed']],520,[180,331])
    p.p('The project demonstrates deterministic reference behavior, optional acceleration, explicit ownership, responsive desktop processing and reproducible test/build commands. It is a recorded-data processing application, not a physical simulator.',710,size=10)

    p.page('01 / SYSTEM DESIGN','A small engine with clear boundaries','I/O, processing, benchmarking and visualization are independent responsibilities.')
    y=p.image(IMG/'architecture.png',140,237);p.caption('Both backends return owned host-side point clouds. The viewer never runs OpenGL calls on the processing worker.',y+8)
    y=p.table(['COMPONENT','RESPONSIBILITY'],[
        ['PCD reader','Reads uncompressed little-endian binary PCD; derives XYZ offsets from header fields and padding.'],
        ['CPU reference','Maps signed integer voxel keys to a weighted centroid accumulator; outputs sorted keys.'],
        ['CUDA backend','Generates keys, radix-sorts, reduces by key, emits centroids and copies results to host.'],
        ['Worker + futures','Serializes I/O and processing; propagates errors; keeps the CUDA workspace alive across jobs.'],
        ['GLFW/OpenGL viewer','Shows matched original/simplified frames, synchronized cameras, detail cells and playback.']],420,[125,386])
    p.p('<b>Ownership:</b> vectors own clouds; RAII buffers own device allocations; a long-lived worker owns its thread-local CUDA workspace. Closing waits for active work. CPU-only builds do not enable the CUDA language or require its toolkit.',y+22,size=10)

    p.page('02 / REFERENCE CONTRACT','One occupied voxel, one centroid','Voxel size controls sampling density while preserving the broad shape of a scan.')
    y=p.image(IMG/'voxel-concept.png',140,220);p.caption('Illustration: a 2D slice of a 3D rule. Blue samples in each occupied cell become one orange centroid. These dots are explanatory, not benchmark data.',y+6)
    y=p.code(['key = (floor(x / s), floor(y / s), floor(z / s))','centroid = arithmetic mean of all points sharing that key'],415)
    y=p.p('For s = 1, points (0.1, 0.2, 0.3) and (0.2, 0.3, 0.4) share key (0, 0, 0). Their representative is (0.15, 0.25, 0.35). A point at x = -0.1 belongs to x-key -1, not 0: floor is essential for negative coordinates.',y+20)
    y=p.h('Boundary and numeric rules',y+23)
    y=p.p('Cells are half-open intervals [k*s, (k+1)*s) in each axis. Exact boundary points move into the next cell. Empty input produces empty output; duplicates collapse into one representative. Nonfinite coordinates, invalid voxel sizes and out-of-range integer keys are rejected.',y)
    p.p('The CPU implementation uses a running weighted mean rather than accumulating a potentially overflowing coordinate sum. Sorted map keys make repeated execution deterministic for the same input order. Floating-point centroid values are not promised to be invariant to arbitrary input permutations.',y+17,size=10)

    p.page('03 / GPU IMPLEMENTATION','Acceleration without changing the contract','The GPU accelerates grouping and centroid reduction; it does not alter the required voxel partition.')
    y=p.table(['STAGE','CUDA IMPLEMENTATION'],[
        ['1. Validate','Host checks finite coordinates, voxel size and signed key range.'],
        ['2. Generate','A fused kernel creates collision-free packed uint64 keys and weighted aggregates.'],
        ['3. Sort','CUB DeviceRadixSort groups equal keys into contiguous runs.'],
        ['4. Reduce','CUB DeviceReduce::ReduceByKey combines centroids and point counts.'],
        ['5. Emit','A kernel emits centroid points; a completed device-to-host copy returns owned output.']],150,[100,411])
    y=p.h('Why the fast path is safe',y+25)
    y=p.p('Mixed-radix packing is used only when the product of the observed key extents fits uint64. Otherwise, a full signed triple-key Thrust sort/reduce fallback preserves correct grouping. The code avoids treating a lossy hash as a unique voxel identifier.',y)
    y=p.h('Workspace reuse and floating-point comparison',y+23)
    y=p.p('Growable RAII device buffers and CUB scratch space persist on the worker thread. Capacity grows when required; it is not reallocated on every warmed frame. Backend calls still include required growth allocation, validation, transfers and synchronization.',y)
    y=p.p('CPU and GPU reduction orders differ, so bitwise equality is the wrong acceptance test. The project compares sorted outputs, counts and finite coordinates with absolute and relative tolerances of 1e-10. The new 900-sample run passed every comparison; maximum observed coordinate error was '+f"{S['max_error']:.3g}." ,y+18)
    p.p('This is tested numerical agreement for the exercised inputs, not a proof for every finite floating-point dataset. Full-range fallback, repeated workspace growth and large signed offsets have dedicated GPU tests.',y+17,size=10,color=MUTED)

    p.page('04 / BENCHMARK EVIDENCE','Measure the completed operation','Fresh session: first 30 timestamp-ordered frames, 3 warm-ups and 30 repetitions per backend per frame.')
    y=p.image(IMG/'benchmark-summary.png',138,250)
    p.table(['BACKEND','P10 (ms)','MEDIAN (ms)','P90 (ms)'],[
        ['CPU','4.933','5.343','7.684'],['CUDA','0.966','1.205','1.762']],415,[130,127,127,127])
    y=p.p(f"The pooled-median ratio is {S['ratio_medians']:.2f}x; the ratio of summed measured CPU and CUDA time is {S['ratio_totals']:.2f}x. These are different summaries of the same work. All 900 paired comparisons passed. P10-P90 describes sample variation across both repetitions and frames, not a confidence interval.",535)
    y=p.p('<b>Included:</b> validation, required allocation, transfers and completed GPU output. <b>Excluded:</b> file loading, rendering, warm-ups, correctness checking and CSV writing. CPU/CUDA measured order alternates. No app capture or compilation was launched concurrently by the authoring workflow during this run.',y+16,size=10)
    p.p('Hardware: Ryzen 7 7735HS; RTX 4050 Laptop, 6,141 MiB; CUDA 13.2.86; driver 595.79; Ubuntu/WSL Release. CPU is the single-threaded ordered-map reference; no parallel CPU baseline was tested. Power, clocks, thermals and OS activity were not controlled. Results are observations, not universal speedups. Raw evidence: output/evidence/benchmark-session.csv.',y+17,size=9,color=MUTED)

    p.page('05 / THE DESKTOP WORKFLOW','Inspect geometry. Replay recorded frames.','The original and simplified panels always represent the same completed frame.')
    y=p.image(IMG/'windows-detail.png',140,265)
    y=p.caption('Native Windows CPU capture, voxel 0.3: local detail shows actual occupied-cell wireframes and their centroid representatives. The crop is explicitly separate from global point counts.',y+8)
    y=p.p('The viewer supports drag/drop and a cached background file browser, largest-file-first listing, orbit/pan/zoom, a fixed local voxel region, and configurable voxel size. A single loaded file offers Play Clip, which discovers neighboring recorded frames without requiring another file selection.',y+20)
    y=p.p('A sequence runs one selected-backend call per frame. It does not repeat benchmark work in the playback loop. Frames are timestamp-ordered; separate clip folders play consecutively. Pause, previous/next, restart, loop and target-FPS controls live in one playback bar.',y+17)
    y=p.p('A second resizable window keeps benchmark settings and results together. It uses the main app input, recalculates on every run, saves distinct CSVs automatically, exposes median latency and P10-P90 graphs, and provides precise hover values. Main-screen metrics update after completion.',y+17)
    p.p('Playback uses an adjustable fixed rate, not exact source timestamp intervals. Actual playback FPS includes the effect of I/O and rendering and differs from reciprocal processing latency. Camera framing stays fixed within a clip; no artificial motion is added to stationary recordings.',y+17,size=10,color=MUTED)

    p.page('06 / PLATFORM QUALIFICATION','Windows: CPU verified. CUDA scoped honestly.','The Linux executable and native Windows executable are distinct builds of the same source.')
    y=p.table(['TARGET','RESULT / EVIDENCE'],[
        ['Windows native CPU','MSVC 19.37, Visual Studio 2022, x64 Release. Build succeeded; all 35 CPU/loader/workflow tests passed.'],
        ['Windows native GUI','GLFW Win32 + OpenGL. Actual renderer: AMD Radeon Graphics. 60 real PCD frames loaded, downsampled and captured.'],
        ['Windows native CUDA','Configure attempted and failed: "No CUDA toolset found." No native Windows CUDA Toolkit/toolset was detected. GPU support is NOT verified here.'],
        ['WSL CUDA','Release build, all 41 tests passed (35 shared + 6 GPU). 60 recorded frames captured using CUDA. Drawing used llvmpipe; compute used NVIDIA CUDA.'],
        ['Cross-build sanity','Native CLI read 55,518 points and emitted 20,345 centroids at voxel 0.1, matching the established WSL reference counts.']],150,[125,386])
    y=p.h('What changed for native builds',y+24)
    y=p.p('An opt-in dependency-fetch option obtains GLFW 3.4 and GoogleTest 1.17.0 into the build tree when installed packages are absent. MSVC warnings found during qualification were corrected. No CUDA Toolkit or system-wide development package was installed on the borrowed Windows machine.',y)
    p.p('Dependencies and media tooling used for this task reside in project build folders. Tests construct their own small fixtures. A failing Windows CUDA configure is recorded as a blocker, not reported as a successful GPU test. The executable requires the appropriate Microsoft VC++ runtime on other computers.',y+18,size=10,color=MUTED)

    p.page('07 / REPRODUCE AND EXTEND','A repeatable starting point','Build commands, evidence and remaining limits are part of the deliverable.')
    y=p.code(['# Native Windows, from a terminal with CMake and Git','cmake -S . -B build/windows-cpu -G "Visual Studio 17 2022" -A x64','  -DCLOUDFORGE_BUILD_VIEWER=ON -DCLOUDFORGE_FETCH_DEPS=ON','  -DBUILD_TESTING=ON -DCLOUDFORGE_ENABLE_CUDA=OFF','cmake --build build/windows-cpu --config Release','ctest --test-dir build/windows-cpu -C Release --output-on-failure'],148)
    y=p.code(['# Ubuntu / WSL (CUDA compiler path may be needed at configure)','cmake --preset cuda-release','cmake --build --preset cuda-release','ctest --preset cuda-release','bash scripts/run-demo.sh'],y+18)
    y=p.p('Commands spanning multiple printed lines must be entered as one command or use the continuation syntax of the chosen shell. Windows package: output/windows/. Tests and CUDA configure failure logs: output/evidence/. Media scripts and the raw timing CSV are retained for review.',y+17,size=10)
    y=p.h('Limits and next experiments',y+19)
    y=p.p('The PCD reader currently accepts uncompressed little-endian binary scalar float32 XYZ; extra attributes are skipped. Playback has no timestamp-accurate scheduling, bounded prefetch or cancellation inside a running kernel. CUDA crossover depends on cloud size and key distribution. Next: workload scaling, per-stage profiling, bounded prefetch and native Windows CUDA qualification once a compatible toolkit is installed.',y,size=10)
    y=p.h('Sources and visual attribution',y+20)
    y=p.p('Project implementation, tests and raw CSVs are primary evidence. External references: <link href="https://dynamic.robots.ox.ac.uk/datasets/oxford-spires/" color="#008ca5">Oxford Spires Dataset - Tao et al.</link>; <link href="https://nvidia.github.io/cccl/cub/api/" color="#008ca5">NVIDIA CUB API</link>; <link href="https://www.glfw.org/" color="#008ca5">GLFW</link>; <link href="https://google.github.io/googletest/" color="#008ca5">GoogleTest</link>.',y,size=9)
    p.p('Screenshots/footage are original app captures visualizing Oxford Spires data. Dataset authors: Yifu Tao, Miguel Angel Munoz-Banon, Lintong Zhang, Jiahao Wang, Lanke Frank Tarimo Fu and Maurice Fallon. Dataset-derived visuals and these media documents are shared under CC BY-NC-SA 4.0; the project code retains its separate MIT license. No raw dataset is bundled.',y+14,size=8.5,color=MUTED)
    p.save()

    b=PDF(OUT/'pdf/cloudForge-portfolio-article.pdf','cloudForge - Portfolio article')
    b.page('PORTFOLIO ARTICLE','Making fewer points mean more.','What building cloudForge taught me about C++, CUDA and honest performance claims.')
    y=b.image(IMG/'cuda-playback.png',140,285)
    y=b.caption('A real recorded scan, displayed twice: original samples on the left; voxel centroids on the right. The scene remains recognizable while the point count falls.',y+8)
    y=b.p('A point-cloud frame can contain tens of thousands of XYZ samples. Repeating that work over a recording turns a small algorithm into a systems problem: how do I read the data, reduce it, keep the interface responsive and prove that acceleration still produces the right result?',y+23)
    y=b.p('I built cloudForge to explore that question with C++20 and an optional CUDA backend. The application loads recorded PCD frames, groups samples into voxels and replaces each occupied cell with its centroid. It then plays the original and simplified sequence side by side.',y+18)
    b.p('One real frame shrank from <b>55,518 points to 9,943</b> at voxel size 0.3 - an <b>82.09% reduction</b>. That is the core idea: fewer samples, while preserving the broad shape. It is not a guarantee that every fine geometric feature survives.',y+18)

    b.page('THE IDEA','Start with a rule you can explain.','One voxel contains many samples, but contributes only one output point.')
    y=b.image(IMG/'voxel-concept.png',143,217)
    y=b.p('For voxel size s, the key is floor(x/s), floor(y/s), floor(z/s). Points with the same key belong to the same group. Their arithmetic mean becomes the output point. Two samples at (0.1, 0.2, 0.3) and (0.2, 0.3, 0.4) produce the centroid (0.15, 0.25, 0.35) when s = 1.',y+22)
    y=b.p('The first important detail was negative coordinates. Truncating -0.1 to zero would put it in the wrong cell. Floor puts it in cell -1. Exact boundaries, empty input, duplicates, NaN, infinity and integer range limits became tests rather than assumptions.',y+17)
    y=b.p('I made the CPU reference deterministic for a fixed input order and kept its output sorted by voxel key. Only after testing that baseline did I introduce CUDA. This gave me something trustworthy to compare against when parallel reduction changed floating-point rounding.',y+17)
    b.p('<b>Lesson:</b> correctness is not a screenshot of a similar-looking cloud. It is a defined contract, explicit edge cases and a repeatable comparison of counts and coordinates.',y+20,color=GREEN)

    b.page('THE EVIDENCE','The GPU did not win automatically.','Launches, transfers, allocation and synchronization can cost more than the useful work.')
    y=b.image(IMG/'benchmark-summary.png',140,247)
    y=b.p('An early recorded run at voxel size 0.1 measured CPU median 5.905 ms and CUDA median 7.326 ms. Moving work to a GPU was not enough. I replaced a repeated-allocation, comparison-sort path with reusable buffers, collision-free packed keys, CUB radix sorting and reduce-by-key. A wide-range fallback preserves signed keys. The CPU baseline remains the single-threaded ordered-map reference, not an optimized parallel CPU implementation.',y+17,size=10.5)
    y=b.p(f"In the new session, 30 real frames with 30 repetitions each produced CPU median {S['cpu_median_ms']:.3f} ms and CUDA median {S['cuda_median_ms']:.3f} ms: a {S['ratio_medians']:.2f}x observed ratio. All 900 comparisons passed. The earlier and current sessions were not controlled side-by-side, so this is not an ablation study or a universal acceleration claim.",y+17,size=10.5)
    y=b.p('The benchmark pauses rendering, fixes the warm-up order and alternates measured order. It includes host validation, required allocation, transfers and completed GPU output. It excludes file loading, warm-up and drawing. CSV rows preserve every sample; graphs expose medians and variation instead of presenting one changing ratio as certainty.',y+17,size=10.5)
    b.p('<b>Lesson:</b> a performance number needs its workload, hardware, timing boundary and correctness evidence. My test laptop used a Ryzen 7 7735HS and RTX 4050 Laptop GPU. Clocks, thermals and background activity still vary.',y+17,size=9.5,color=MUTED)

    b.page('THE ENGINEERING','A project becomes useful at its edges.','The application around the kernel made the work worth doing.')
    y=b.image(IMG/'windows-detail.png',140,235)
    y=b.p('The file browser initially stalled on repeated Windows-drive metadata checks. Caching entry metadata and moving scans to a worker made navigation responsive. A long-lived worker also kept the CUDA workspace alive. Original and simplified frames now appear as a matched pair, with synchronized pan, orbit and zoom.',y+18,size=10.5)
    y=b.p('Recorded playback uses one processing call per frame and offers pause, stepping, restart, looping and speed controls. Benchmarking has its own resizable window, but reuses the loaded input. Running it again recalculates and saves a fresh report automatically. These are small interface decisions with large effects on whether the result is understandable.',y+17,size=10.5)
    y=b.p('I also tested a native Windows CPU build: all 35 tests passed and 60 real frames played successfully. The WSL CUDA build passed 41 tests. Native Windows CUDA remains unverified because the required Windows toolkit/toolset is missing. I would rather state that boundary than imply platform coverage I have not demonstrated.',y+17,size=10.5)
    y=b.p('<b>What I would build next:</b> a point-count scaling study, bounded prefetch for smoother playback, per-stage GPU profiling and native Windows CUDA qualification. The next optimization should come from measurement, not from choosing the most impressive-looking ratio.',y+17,size=10.5)
    b.p('Screenshots: actual cloudForge app captures. Data: Oxford Spires Dataset, Tao et al., <link href="https://dynamic.robots.ox.ac.uk/datasets/oxford-spires/" color="#008ca5">official project</link>, CC BY-NC-SA 4.0. This illustrated article shares that media license; cloudForge source code remains MIT. Benchmark evidence and hardware details accompany the technical report.',y+18,size=8.5,color=MUTED)
    b.save()

SCENES=[
 (8,'cloudForge','Fewer points. Same 3D scene.','title','A C++20 point-cloud player with an optional CUDA processing backend.'),
 (10,'01 / Load and inspect','A desktop app built around recorded 3D data.','windows-playback.png','Load a PCD file or a sequence. Both views always show the same completed frame.'),
 (12,'02 / The voxel rule','Many samples become one representative.','voxel-concept.png','Divide space into cells. Group points by cell, then output one centroid per occupied voxel.'),
 (12,'03 / Native Windows playback','Original on the left. Simplified on the right.','native','Actual native Windows CPU app frames, replayed at 10 FPS. Both views stay synchronized.'),
 (12,'04 / CUDA playback in WSL','The same sequence, processed on the NVIDIA GPU.','cuda','Actual WSL CUDA app frames. The rendering backend is separate from the compute backend.'),
 (12,'05 / Look inside a voxel','See the occupied cells and their centroids.','cuda-detail.png','Local detail makes density changes visible. The crop counts are separate from the global counts.'),
 (12,'06 / Engineer the pipeline','Keep the contract. Accelerate the grouping.','architecture.png','A tested CPU reference anchors the CUDA implementation. A worker keeps processing off the UI thread.'),
 (10,'07 / Benchmark honestly','Measure completed work, not an unfinished kernel launch.','benchmark-results.png','Warm up, alternate backend order, check every output pair, and keep every measured sample in CSV.'),
 (12,'08 / Read the evidence',f"{S['ratio_medians']:.2f}x observed ratio in one measured session.",'benchmark-summary.png','30 frames, 30 repetitions, 900 checks passed. This result is specific to the workload and laptop.'),
 (10,'09 / Platform qualification','Windows CPU tested. WSL CUDA tested.','platform','35 native Windows tests passed. The WSL CUDA build passed 41. Native Windows CUDA still needs its toolkit.'),
 (10,'cloudForge / Next experiments','Build. Measure. Explain. Repeat.','outro','Next: workload scaling, bounded prefetch, per-stage profiling and native Windows CUDA validation.')]

def fit(im,box):
    result=ImageOps.contain(im,(box[2]-box[0],box[3]-box[1]),Image.Resampling.LANCZOS)
    return result,(box[0]+(box[2]-box[0]-result.width)//2,box[1]+(box[3]-box[1]-result.height)//2)
def wrap_pixels(text,f,max_width):
    lines=[];line=''
    for word in text.split():
        candidate=(line+' '+word).strip()
        if f.getlength(candidate)>max_width and line:lines.append(line);line=word
        else:line=candidate
    if line:lines.append(line)
    return lines
@lru_cache(maxsize=8)
def frame_image(kind,index):
    folder=ROOT/('build/media-native-frames' if kind=='native' else 'build/media-cuda-frames')
    im=Image.open(folder/f'frame-{index:05}.bmp').convert('RGB')
    # Crop to actual paired views, retain both labels and the recording's geometry.
    return im.crop((274,298,1582,880))

def make_video():
    import imageio_ffmpeg
    exe=imageio_ffmpeg.get_ffmpeg_exe();w,h,fps=1920,1080,15
    path=OUT/'video/cloudForge-demo.mp4'
    args=[exe,'-y','-hide_banner','-loglevel','warning','-f','rawvideo','-pixel_format','rgb24','-video_size',f'{w}x{h}','-framerate',str(fps),'-i','pipe:0','-an','-c:v','libx264','-preset','fast','-crf','21','-pix_fmt','yuv420p','-movflags','+faststart',str(path)]
    process=subprocess.Popen(args,stdin=subprocess.PIPE)
    time=0;srt=[];story=[]
    def ts(seconds):
        ms=round(seconds*1000);return f'{ms//3600000:02}:{ms//60000%60:02}:{ms//1000%60:02},{ms%1000:03}'
    try:
        for scene_idx,(seconds,kicker,title,asset,subtitle) in enumerate(SCENES):
            start=time;time+=seconds;story.append(f'{ts(start)} - {ts(time)} | {kicker}\n{subtitle}\n')
            srt.append(f'{scene_idx+1}\n{ts(start)} --> {ts(time)}\n{subtitle}\n')
            base=Image.new('RGB',(w,h),NAVY);d=ImageDraw.Draw(base)
            d.rectangle((0,0,w,8),fill=CYAN);d.text((72,30),kicker.upper(),font=font(25,True),fill='#57cadd')
            for j,line in enumerate(wrap_pixels(title,font(45,True),1770)):d.text((72,72+j*52),line,font=font(45,True),fill='white')
            if asset=='title':
                d.text((110,320),'cloudForge',font=font(134,True),fill='white')
                d.text((118,507),'C++20   /   CUDA   /   Recorded point clouds',font=font(40),fill='#b7d2e0')
                d.text((118,610),'One voxel. One centroid. A measurable result.',font=font(39,True),fill='#57cadd')
            elif asset=='platform':
                for j,(head,body,col) in enumerate([('WINDOWS NATIVE / CPU','Build + 35 tests + 60 recorded frames: PASS',BLUE),('UBUNTU WSL / CUDA','41 tests + GPU playback + 900 timing checks: PASS',ORANGE),('WINDOWS NATIVE / CUDA','Not verified: Windows CUDA toolset is missing.', '#c1cbd6')]):
                    yy=235+j*205;d.rounded_rectangle((85,yy,1835,yy+170),18,fill='#13253d');d.text((120,yy+28),head,font=font(34,True),fill=col);d.text((120,yy+86),body,font=font(34),fill='white')
            elif asset=='outro':
                d.text((110,255),'A reference-first GPU engineering project.',font=font(54,True),fill='white')
                d.text((110,365),'Code: MIT  |  No raw dataset is bundled.',font=font(32),fill='#c1d6e5')
                credit=['Data / Oxford Spires Dataset - Tao et al.','Yifu Tao, Miguel Angel Munoz-Banon, Lintong Zhang,','Jiahao Wang, Lanke Frank Tarimo Fu and Maurice Fallon.','Dataset-derived visuals: CC BY-NC-SA 4.0','dynamic.robots.ox.ac.uk/datasets/oxford-spires/']
                for j,line in enumerate(credit):d.text((110,470+j*55),line,font=font(28),fill='#a4bccf')
            elif asset not in ('native','cuda'):
                im=Image.open(IMG/asset).convert('RGB');res,pos=fit(im,(70,165,1850,900));base.paste(res,pos)
            for fi in range(seconds*fps):
                im=base.copy()
                if asset in ('native','cuda'):
                    raw=frame_image(asset,int(fi/fps*10)%60);res,pos=fit(raw,(70,180,1850,895));im.paste(res,pos)
                draw=ImageDraw.Draw(im);draw.rectangle((0,925,w,1080),fill='#060e1b')
                lines=wrap_pixels(subtitle,font(31),1740)
                for j,line in enumerate(lines):draw.text((90,950+j*44),line,font=font(31),fill='white')
                progress=(start+fi/fps)/sum(s[0] for s in SCENES)
                draw.rectangle((72,1050,1848,1054),fill='#26394d');draw.rectangle((72,1050,72+int(1776*progress),1054),fill='#40bcd1')
                process.stdin.write(im.tobytes())
            print(f'Encoded scene {scene_idx+1}/{len(SCENES)}: {kicker}',flush=True)
        process.stdin.close();code=process.wait()
        if code:raise RuntimeError(f'ffmpeg failed: {code}')
    except BaseException:
        process.kill();raise
    (OUT/'video/cloudForge-demo.srt').write_text('\n'.join(srt),encoding='utf-8')
    (OUT/'video/voiceover-guide.md').write_text('# cloudForge voice-over guide\n\nThe MP4 is intentionally silent, with burned-in subtitles. Record your voice-over against these scene timings. The SRT is an editable copy of the captions. Playback captures are replayed at 10 FPS; each six-second capture appears twice. Timing numbers shown in captures are illustrative, not the headline benchmark session.\n\n'+'\n'.join(story),encoding='utf-8')
    subprocess.run([exe,'-hide_banner','-i',str(path),'-f','null','-'],stdout=subprocess.DEVNULL,stderr=(OUT/'evidence/video-decode.txt').open('w'),check=True)
    for t in [4,22,38,50,62,90,116]:
        subprocess.run([exe,'-y','-loglevel','error','-ss',str(t),'-i',str(path),'-frames:v','1',str(OUT/f'images/video-check-{t:03}.png')],check=True)

def render_pdfs():
    import pymupdf
    dest=ROOT/'tmp/pdfs';dest.mkdir(parents=True,exist_ok=True)
    for file in (OUT/'pdf').glob('*.pdf'):
        doc=pymupdf.open(file);thumbs=[]
        for i,page in enumerate(doc):
            pix=page.get_pixmap(matrix=pymupdf.Matrix(1.25,1.25));target=dest/f'{file.stem}-{i+1}.png';pix.save(str(target))
            im=Image.open(target).convert('RGB');im.thumbnail((350,495));thumbs.append(im)
        cols=4;montage=Image.new('RGB',(cols*370,math.ceil(len(thumbs)/cols)*520),'#dbe3ea')
        for i,im in enumerate(thumbs):montage.paste(im,((i%cols)*370+10,(i//cols)*520+10))
        montage.save(dest/f'{file.stem}-contact.png');print(file.name,len(doc),'pages')

if __name__=='__main__':
    args=argparse.ArgumentParser();args.add_argument('--video',action='store_true');a=args.parse_args()
    save_pngs();diagram();pipeline();benchmark_chart();reports();render_pdfs()
    if a.video:make_video()
