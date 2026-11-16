// Descrição: renderiza o XPS original do spooler com WPF, preservando as dimensões da página.
using System;
using System.Globalization;
using System.IO;
using System.IO.Compression;
using System.Text;
using System.Windows;
using System.Windows.Documents;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Xps.Packaging;
using System.Xml;

class RenderXps {
    static readonly CultureInfo Cultura = CultureInfo.InvariantCulture;
    static void ConferirPacote(string caminho) {
        using (var arquivo = File.OpenRead(caminho))
        using (var zip = new ZipArchive(arquivo, ZipArchiveMode.Read)) {
            if (zip.Entries.Count > 512) throw new InvalidDataException("XPS com partes demais.");
            long total = 0;
            foreach (var parte in zip.Entries) {
                total += parte.Length;
                if (parte.Length > 32 * 1024 * 1024 || total > 128 * 1024 * 1024)
                    throw new InvalidDataException("XPS excede o limite descompactado.");
                if (!parte.FullName.EndsWith(".rels", StringComparison.OrdinalIgnoreCase) &&
                    !parte.FullName.EndsWith(".fpage", StringComparison.OrdinalIgnoreCase)) continue;
                using (var stream = parte.Open())
                using (var xml = XmlReader.Create(stream, new XmlReaderSettings {
                    DtdProcessing = DtdProcessing.Prohibit, XmlResolver = null, MaxCharactersInDocument = 32 * 1024 * 1024
                })) {
                    while (xml.Read()) {
                        if (xml.NodeType != XmlNodeType.Element) continue;
                        if (xml.GetAttribute("TargetMode") == "External")
                            throw new InvalidDataException("XPS com recurso externo recusado.");
                        foreach (string atributo in new[] { "FontUri", "ImageSource", "Source", "NavigateUri" }) {
                            string uri = xml.GetAttribute(atributo);
                            if (uri != null && (uri.Contains("://") || uri.StartsWith("file:", StringComparison.OrdinalIgnoreCase)))
                                throw new InvalidDataException("XPS com URI externa recusado.");
                        }
                    }
                }
            }
        }
    }

    [STAThread]
    static int Main(string[] args) {
        try {
            if (args.Length != 1) throw new ArgumentException("Informe uma captura XPS local.");
            string pasta = Path.GetFullPath(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "capturas"));
            string entrada = Path.GetFullPath(args[0]);
            if (!String.Equals(Path.GetDirectoryName(entrada), pasta, StringComparison.OrdinalIgnoreCase) ||
                !Path.GetFileName(entrada).StartsWith("trabalho-", StringComparison.Ordinal) ||
                !entrada.EndsWith(".xps", StringComparison.OrdinalIgnoreCase))
                throw new ArgumentException("Arquivo fora das capturas do simulador.");
            if (new FileInfo(entrada).Length > 32 * 1024 * 1024) throw new InvalidDataException("XPS grande demais.");
            ConferirPacote(entrada);
            string raiz = Path.Combine(pasta, Path.GetFileNameWithoutExtension(entrada));
            var medidas = new StringBuilder();
            using (var doc = new XpsDocument(entrada, FileAccess.Read)) {
                var sequencia = doc.GetFixedDocumentSequence();
                if (sequencia == null) throw new InvalidDataException("Documento XPS vazio.");
                var paginador = sequencia.DocumentPaginator;
                paginador.ComputePageCount();
                int quantidade = paginador.PageCount;
                if (quantidade < 1 || quantidade > 20) throw new InvalidDataException("XPS excede 20 páginas.");
                medidas.AppendLine(quantidade.ToString(Cultura));
                long pixelsTotais = 0;
                for (int i = 0; i < quantidade; ++i) {
                    using (var pagina = paginador.GetPage(i)) {
                        double w = pagina.Size.Width, h = pagina.Size.Height;
                        if (Double.IsNaN(w) || Double.IsNaN(h) || Double.IsInfinity(w) || Double.IsInfinity(h) ||
                            w <= 0 || h <= 0 || w > 6000 || h > 27000)
                            throw new InvalidDataException("Dimensões XPS inválidas.");
                        double escala = Math.Min(203.0 / 96.0, Math.Min(1600.0 / w, 10000.0 / h));
                        int pw = (int)Math.Ceiling(w * escala), ph = (int)Math.Ceiling(h * escala);
                        pixelsTotais += (long)pw * ph;
                        if ((long)pw * ph > 12000000 || pixelsTotais > 40000000)
                            throw new InvalidDataException("XPS excede limite de pixels.");
                        var visual = new DrawingVisual();
                        using (var desenho = visual.RenderOpen()) {
                            desenho.DrawRectangle(Brushes.White, null, new Rect(0, 0, w, h));
                            var pincel = new VisualBrush(pagina.Visual) {
                                ViewboxUnits = BrushMappingMode.Absolute, Viewbox = new Rect(0, 0, w, h),
                                ViewportUnits = BrushMappingMode.Absolute, Viewport = new Rect(0, 0, w, h),
                                Stretch = Stretch.None, AlignmentX = AlignmentX.Left, AlignmentY = AlignmentY.Top
                            };
                            desenho.DrawRectangle(pincel, null, new Rect(0, 0, w, h));
                        }
                        var imagem = new RenderTargetBitmap(pw, ph, 96 * escala, 96 * escala, PixelFormats.Pbgra32);
                        imagem.Render(visual);
                        var encoder = new PngBitmapEncoder();
                        encoder.Frames.Add(BitmapFrame.Create(imagem));
                        using (var stream = File.Create(raiz + "-p" + (i + 1) + ".png")) encoder.Save(stream);
                        medidas.AppendLine((w * 25.4 / 96).ToString("R", Cultura) + " " + (h * 25.4 / 96).ToString("R", Cultura));
                    }
                }
            }
            File.WriteAllText(raiz + ".medidas", medidas.ToString(), new UTF8Encoding(false));
            return 0;
        } catch (Exception ex) {
            Console.Error.WriteLine(ex.Message);
            return 1;
        }
    }
}
