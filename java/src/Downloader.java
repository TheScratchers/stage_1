import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.LocalDateTime;
import java.time.format.DateTimeFormatter;
import java.util.regex.Pattern;

public class Downloader {
    private static final String START_MARKER = "*** START OF THE PROJECT GUTENBERG EBOOK";
    private static final String END_MARKER = "*** END OF THE PROJECT GUTENBERG EBOOK";
    private static final String DATALAKE_PATH = "data/datalake/";

    public static boolean downloadBook(int bookId, String url) {
        try {
            HttpClient client = HttpClient.newBuilder()
                    .followRedirects(HttpClient.Redirect.NORMAL)
                    .build();
            
            HttpRequest request = HttpRequest.newBuilder()
                    .uri(URI.create(url))
                    .build();

            System.out.println("[DOWNLOADER] Fetching book ID " + bookId + " from: " + url);
            HttpResponse<String> response = client.send(request, HttpResponse.BodyHandlers.ofString());
            String text = response.body();

            if (!text.contains(START_MARKER) || !text.contains(END_MARKER)) {
                System.out.println("[DOWNLOADER] Error: Gutenberg markers not found in the text.");
                return false;
            }

            String[] headerAndRest = text.split(Pattern.quote(START_MARKER), 2);
            String header = headerAndRest[0];
            String[] bodyAndFooter = headerAndRest[1].split(Pattern.quote(END_MARKER), 2);
            String body = bodyAndFooter[0];

            LocalDateTime now = LocalDateTime.now();
            String dateFolder = now.format(DateTimeFormatter.ofPattern("yyyyMMdd"));
            String hourFolder = now.format(DateTimeFormatter.ofPattern("HH"));
            
            Path outputPath = Paths.get(DATALAKE_PATH, dateFolder, hourFolder);
            Files.createDirectories(outputPath);

            Files.writeString(outputPath.resolve(bookId + ".header.txt"), header.trim());
            Files.writeString(outputPath.resolve(bookId + ".body.txt"), body.trim());

            System.out.println("[DOWNLOADER] Successfully saved to: " + outputPath.toString());
            return true;
        } catch (Exception e) {
            System.err.println("[DOWNLOADER] Failed to download book ID " + bookId);
            e.printStackTrace();
            return false;
        }
    }
    
    public static void main(String[] args) {
        // Example 1: Standard Gutenberg URL
        int bookId1 = 23;
        String url1 = "https://www.gutenberg.org/cache/epub/" + bookId1 + "/pg" + bookId1 + ".txt";
        downloadBook(bookId1, url1);

    }
}