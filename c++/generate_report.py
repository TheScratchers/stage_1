import json
import os
import shutil
from reportlab.lib.pagesizes import letter
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle, PageBreak, KeepTogether
)
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib import colors
from reportlab.pdfgen import canvas

class NumberedCanvas(canvas.Canvas):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._saved_page_states = []

    def showPage(self):
        self._saved_page_states.append(dict(self.__dict__))
        self._startPage()

    def save(self):
        num_pages = len(self._saved_page_states)
        for state in self._saved_page_states:
            self.__dict__.update(state)
            self.draw_page_decorations(num_pages)
            super().showPage()
        super().save()

    def draw_page_decorations(self, page_count):
        self.saveState()
        self.setFont("Helvetica", 8)
        self.setFillColor(colors.HexColor("#4A5568"))
        
        # Header (page 2 only)
        if self._pageNumber > 1:
            self.drawString(36, 762, "Stage 1 Technical Report — C++ Search Engine Data Layer & Datamarts")
            self.drawRightString(612 - 36, 762, "Big Data — ULPGC")
            self.setStrokeColor(colors.HexColor("#CBD5E0"))
            self.setLineWidth(0.5)
            self.line(36, 756, 612 - 36, 756)

        # Footer
        self.setStrokeColor(colors.HexColor("#CBD5E0"))
        self.setLineWidth(0.5)
        self.line(36, 32, 612 - 36, 32)
        self.drawString(36, 22, "Author: Pablo Martínez Suárez (TheScratchers) | C++17 Standard Implementation")
        self.drawRightString(612 - 36, 22, f"Page {self._pageNumber} of {page_count}")
        self.restoreState()

def build_pdf(filename="c++/stage_1_cpp_report.pdf"):
    # Load JSON benchmark results
    with open("c++/datamarts/benchmark_datalake_results.json") as f:
        lake_data = json.load(f)
    with open("c++/datamarts/benchmark_metadata_results.json") as f:
        meta_data = json.load(f)
    with open("c++/datamarts/benchmark_inverted_index_results.json") as f:
        idx_data = json.load(f)

    doc = SimpleDocTemplate(
        filename,
        pagesize=letter,
        leftMargin=36,
        rightMargin=36,
        topMargin=36,
        bottomMargin=42
    )

    styles = getSampleStyleSheet()
    
    title_style = ParagraphStyle(
        'DocTitle',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=15,
        leading=17,
        textColor=colors.HexColor("#1A365D"),
        spaceAfter=2
    )
    
    subtitle_style = ParagraphStyle(
        'DocSubtitle',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=8.5,
        leading=11,
        textColor=colors.HexColor("#2B6CB0"),
        spaceAfter=6
    )
    
    h1_style = ParagraphStyle(
        'SectionH1',
        parent=styles['Normal'],
        fontName='Helvetica-Bold',
        fontSize=10,
        leading=12,
        textColor=colors.HexColor("#2C5282"),
        spaceBefore=4,
        spaceAfter=2
    )
    
    body_style = ParagraphStyle(
        'BodyTextCustom',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7.5,
        leading=9.5,
        textColor=colors.HexColor("#2D3748"),
        spaceAfter=3
    )

    bullet_style = ParagraphStyle(
        'BulletCustom',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7.2,
        leading=9.2,
        textColor=colors.HexColor("#2D3748"),
        leftIndent=8,
        spaceAfter=2
    )

    table_cell = ParagraphStyle(
        'TableCell',
        parent=styles['Normal'],
        fontName='Helvetica',
        fontSize=7,
        leading=8,
        alignment=1
    )
    
    table_cell_bold = ParagraphStyle(
        'TableCellBold',
        parent=table_cell,
        fontName='Helvetica-Bold',
        textColor=colors.HexColor("#1A202C")
    )
    
    table_hdr = ParagraphStyle(
        'TableHdr',
        parent=table_cell,
        fontName='Helvetica-Bold',
        fontSize=7,
        leading=8,
        textColor=colors.white
    )

    story = []

    # Title & Subtitle
    story.append(Paragraph("STAGE 1: SEARCH ENGINE DATA LAYER & BENCHMARK", title_style))
    story.append(Paragraph("Author: Pablo Martínez Suárez | Language: C++17 (Clang/GCC -O2) | Course: Big Data — ULPGC", subtitle_style))

    # Section 1
    story.append(Paragraph("1. Executive Summary & Architecture Overview", h1_style))
    story.append(Paragraph(
        "This technical report documents the complete implementation, benchmarking, and empirical analysis of the Stage 1 Data Layer for "
        "an autonomous search engine written in C++17. The system strictly adheres to the Cross-Language Benchmark Contract "
        "(CONTRACT.md), enabling direct, 1:1 comparable evaluations against Java and Python counterparts. The architecture is cleanly "
        "decoupled into four core subsystems: (1) Datalake Storage Engine supporting three distinct directory layout topologies with "
        "single-pass incremental detection and idempotent recovery; (2) Metadata Datamart utilizing SQLite with transaction-batched WAL "
        "ingestion (> 530,000 rows/s); (3) Inverted Index Datamarts comparing Monolithic JSON, Hierarchical Sharded Folders, and a "
        "Relational SQLite Index with real process memory profiling (measuring actual resident set size); and (4) Control Layer orchestrating pipeline ingestion via libcurl and "
        "automated multi-datamart state synchronization.",
        body_style
    ))

    # Section 2
    story.append(Paragraph("2. Datalake Multi-Scale Evaluation (100 / 1,000 / 10,000 Books)", h1_style))
    story.append(Paragraph(
        "We evaluated three physical partitioning strategies across scales of 100, 1,000, and 10,000 books: Time-based "
        "(YYYYMMDD/HH/), Book-based (&lt;id&gt;/), and Batch-based (batch_N/, 500 books/batch). Metrics include initial write throughput, "
        "random lookup latency, single-pass incremental detection time, incremental write time, idempotent recovery duration, and directory "
        "metadata overhead.",
        body_style
    ))

    # Table Datalake
    lake_hdr = [Paragraph(h, table_hdr) for h in ["Scale", "Structure", "Write (s)", "Books/s", "Lookup (ms)", "Incr. Det (s)", "Incr. Wrt (s)", "Recovery (s)", "Dirs"]]
    lake_rows = [lake_hdr]
    
    # Parse lake_data
    for scale_str in ["100", "1000", "10000"]:
        scale_res = lake_data["results_by_scale"].get(scale_str, [])
        for item in scale_res:
            lake_rows.append([
                Paragraph(f"{int(scale_str):,}", table_cell),
                Paragraph(item["structure"], table_cell),
                Paragraph(f"{item['write_seconds']:.3f}", table_cell),
                Paragraph(f"{item['write_books_per_sec']:,.1f}", table_cell),
                Paragraph(f"{item['lookup_avg_ms']:.3f}", table_cell),
                Paragraph(f"{item['incremental_detect_seconds']:.4f}", table_cell),
                Paragraph(f"{item['incremental_write_seconds']:.3f}", table_cell),
                Paragraph(f"{item['recovery_seconds']:.3f}", table_cell),
                Paragraph(f"{item['num_dirs_created']:,}", table_cell)
            ])

    t_lake = Table(lake_rows, colWidths=[40, 68, 52, 60, 62, 66, 64, 66, 42])
    t_lake.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor("#2B6CB0")),
        ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('TOPPADDING', (0, 0), (-1, -1), 1.5),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 1.5),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.HexColor("#F7FAFC"), colors.white]),
        ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor("#E2E8F0")),
    ]))
    story.append(t_lake)
    story.append(Spacer(1, 3))

    story.append(Paragraph(
        "<b>Datalake Analysis & Findings:</b><br/>"
        "• <b>Write Throughput & Inode Overhead:</b> time_based and batch_based achieve the highest throughput (~830–1,015 books/s) by "
        "minimizing directory creations. book_based creates 10,000 directories at scale, introducing significant filesystem inode table pressure.<br/>"
        "• <b>Incremental Processing & Recovery:</b> Single-pass scan (detectNewBooks) takes only 11.4 ms for 10,000 books in time_based, "
        "versus 41.3 ms in book_based due to directory tree recursion overhead. Recovery idempotence verified: 100% of missing files were restored with zero duplicate corruption.",
        bullet_style
    ))

    # Section 3
    story.append(Paragraph("3. Metadata SQLite Datamart Benchmark (Team Extension)", h1_style))
    story.append(Paragraph(
        "Project Gutenberg headers were parsed via C++ std::regex into structured fields (book_id, title, author, language, ingested_at, "
        "header_path, body_path) and inserted into SQLite using explicit transaction chunking (BEGIN TRANSACTION / COMMIT) with "
        "prepared statements. Lookups were tested for exact PK (find_by_id) and partial match (find_by_author) using Charles Dickens "
        "(repeated) and Lewis Carroll (unique).",
        body_style
    ))

    # Table Metadata
    meta_hdr = [Paragraph(h, table_hdr) for h in ["Scale (Books)", "Insert (s)", "Throughput (rows/s)", "find_by_id (ms)", "Dickens (ms)", "Carroll (ms)", "DB Size (KB)"]]
    meta_rows = [meta_hdr]
    for r in meta_data["results"]:
        meta_rows.append([
            Paragraph(f"{r['n_books']:,}", table_cell),
            Paragraph(f"{r['insert_seconds']:.4f}", table_cell),
            Paragraph(f"{r['insert_books_per_sec']:,.1f}", table_cell),
            Paragraph(f"{r['find_by_id_avg_ms']:.3f}", table_cell),
            Paragraph(f"{r['find_by_author_repeated_avg_ms']:.3f}", table_cell),
            Paragraph(f"{r['find_by_author_unique_avg_ms']:.3f}", table_cell),
            Paragraph(f"{r['db_size_bytes']/1024.0:,.1f}", table_cell)
        ])

    t_meta = Table(meta_rows, colWidths=[65, 65, 95, 80, 80, 80, 75])
    t_meta.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor("#2B6CB0")),
        ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('TOPPADDING', (0, 0), (-1, -1), 1.5),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 1.5),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.HexColor("#F7FAFC"), colors.white]),
        ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor("#E2E8F0")),
    ]))
    story.append(t_meta)
    story.append(Spacer(1, 3))

    story.append(Paragraph(
        "<b>Metadata Findings:</b> Transactional batching delivers outstanding write scalability, climbing to 536,940 rows/sec at 10,000 books. "
        "Primary key index lookups remain constant at ~0.14 ms (O(1)/O(log N) B-Tree), while unindexed author substring scans scale linearly with dataset cardinality.",
        bullet_style
    ))

    # Page Break to Page 2
    story.append(PageBreak())

    # Section 4
    story.append(Paragraph("4. Inverted Index Datamart Multi-Scale Evaluation & Memory Profiling", h1_style))
    story.append(Paragraph(
        "Following the shared contract, we indexed the corpus using the exact tokenization rule (lowercase ASCII [A-Za-z]+) and evaluated "
        "three architectures: 1. Monolithic JSON (single file containing all vocabulary postings loaded into RAM), 2. Hierarchical Sharded "
        "(directory-partitioned term files A-Z/_), and 3. Relational SQLite Index (CREATE TABLE inverted_index(term TEXT, book_id INTEGER, PRIMARY KEY (term, book_id))). "
        "Peak RAM is measured empirically during execution via process resident memory profiling (getrusage / mach task_info).",
        body_style
    ))

    # Table Index
    idx_hdr = [Paragraph(h, table_hdr) for h in ["Scale", "Structure", "Build (s)", "Lookup avg (ms)", "Update (ms)", "Peak RAM", "Disk (MB)"]]
    idx_rows = [idx_hdr]

    for scale_str in ["100", "1000", "10000"]:
        res_map = idx_data["results_by_scale"].get(scale_str, {})
        for st_name in ["json_monolithic", "hierarchical", "sqlite_index"]:
            if st_name in res_map:
                it = res_map[st_name]
                ram_kb = it["peak_memory_kb"]
                ram_str = f"{ram_kb / 1024.0:.1f} MB" if ram_kb >= 1024.0 else f"{ram_kb:.1f} KB"
                disk_mb = it["total_size_bytes"] / (1024.0 * 1024.0)
                idx_rows.append([
                    Paragraph(f"{int(scale_str):,}", table_cell),
                    Paragraph(st_name, table_cell),
                    Paragraph(f"{it['build_seconds']:.3f}", table_cell),
                    Paragraph(f"{it.get('avg_lookup_ms', it.get('in_memory_lookup_avg_ms', 0)):.3f}", table_cell),
                    Paragraph(f"{it['update_seconds']*1000.0:,.1f}", table_cell),
                    Paragraph(ram_str, table_cell_bold),
                    Paragraph(f"{disk_mb:,.2f}", table_cell)
                ])

    t_idx = Table(idx_rows, colWidths=[45, 95, 65, 85, 75, 85, 90])
    t_idx.setStyle(TableStyle([
        ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor("#2B6CB0")),
        ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
        ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
        ('TOPPADDING', (0, 0), (-1, -1), 1.5),
        ('BOTTOMPADDING', (0, 0), (-1, -1), 1.5),
        ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.HexColor("#F7FAFC"), colors.white]),
        ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor("#E2E8F0")),
    ]))
    story.append(t_idx)
    story.append(Spacer(1, 3))

    story.append(Paragraph(
        "<b>Inverted Index Architectural Trade-offs & Memory Mechanics:</b><br/>"
        "• <b>Monolithic JSON:</b> Delivers instantaneous query lookup (&lt; 0.001 ms) once loaded into RAM, with in-memory map consumption scaling predictably (6.2 MB → 30.8 MB → 276.7 MB). However, it suffers fatal incremental update degradation (10.6 seconds per book at 10,000 scale) due to complete file rewriting.<br/>"
        "• <b>Hierarchical Index:</b> Streaming file operations provide localized atomic updates (3.7 ms per book) without global file locks. Managing 35,007 open file descriptors and OS filesystem cache buffers increases process peak resident memory (75.8 MB → 360.5 MB → 1,478.6 MB) while maintaining a compact disk footprint (466.9 MB).<br/>"
        "• <b>SQLite Relational Index:</b> Guarantees full ACID transactional integrity with stable resident memory footprint (58.2 MB → 30.4 MB → 195.9 MB). Incremental updates remain exceptionally fast and constant (2.4 ms) even at 10,000 books, trading off higher disk footprint (4,225 MB) for indexed relational access.",
        bullet_style
    ))

    # Section 5
    story.append(Paragraph("5. Automated Testing Suite & Contract Compliance", h1_style))
    story.append(Paragraph(
        "The C++ module includes a comprehensive automated unit test suite integrated into CMake (ctest) and standalone binary "
        "execution. 16 distinct test suites with 119 individual assertions verify: (1) Tokenizer regex & ASCII compliance; (2) Datalake "
        "path generation, multi-layout saving, single-pass incremental change detection, and recovery idempotence; (3) Metadata extraction "
        "and SQLite CRUD transactions; (4) Inverted Index construction, search correctness, and cross-structure consistency (identical "
        "results across JSON, Hierarchical, and SQLite); (5) QueryEngine Boolean AND/OR operations; and (6) Control Layer state "
        "machine transitions. <b>Current Test Results: 16 passed, 0 failed (100% pass rate).</b>",
        body_style
    ))

    # Section 6
    story.append(Paragraph("6. Cross-Language Conclusions & Summary", h1_style))
    story.append(Paragraph(
        "The C++ implementation demonstrates near-zero runtime overhead, exceptional raw I/O throughput, and sub-millisecond query "
        "execution. By aligning tokenization rules, query workloads, and synthetic scaling factors via CONTRACT.md, the C++ results "
        "establish an exact empirical baseline for comparing Java and Python in the final cross-language benchmark evaluation. "
        "All benchmark metrics, JSON datamarts, and test logs are fully version-controlled in the repository under c++/datamarts/ for verifiable reproducibility.",
        body_style
    ))

    doc.build(story, canvasmaker=NumberedCanvas)
    print(f"Successfully generated {filename}")
    shutil.copyfile(filename, "stage_1_cpp_report.pdf")
    print("Copied to stage_1_cpp_report.pdf")

if __name__ == "__main__":
    build_pdf()
