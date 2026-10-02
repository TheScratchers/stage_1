package com.thescratchers.searchengine.datamarts;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

/**
 * Utilidad de tokenizacion de texto para el motor de busqueda.
 *
 * <p><b>Contrato estricto</b> (debe respetarse en todos los lenguajes del benchmark):
 * <ol>
 *   <li>Convertir el texto completo a minusculas.</li>
 *   <li>Extraer unicamente bloques de letras ASCII con la regex {@code [A-Za-z]+}.</li>
 *   <li><em>Sin</em> stemming, lematizacion ni eliminacion de stopwords.</li>
 *   <li><em>Sin</em> digitos: "123" no genera ningun token.</li>
 * </ol>
 * </p>
 *
 * <p>Ejemplo de referencia para validar implementaciones en otros lenguajes:
 * <pre>
 *   tokenize("Hello World, 123! Testing.")
 *   // => ["hello", "world", "testing"]
 * </pre>
 * </p>
 */
public final class TextTokenizer {

    /** Regex que captura exclusivamente bloques de letras ASCII. */
    private static final Pattern WORD_PATTERN = Pattern.compile("[A-Za-z]+");

    // Clase puramente estatica; no debe instanciarse.
    private TextTokenizer() {}

    /**
     * Tokeniza un texto plano siguiendo el contrato del benchmark.
     *
     * @param text texto de entrada (puede ser null o vacio)
     * @return lista inmutable de tokens en minusculas; nunca null
     */
    public static List<String> tokenize(String text) {
        if (text == null || text.isEmpty()) {
            return Collections.emptyList();
        }

        String lower = text.toLowerCase();
        Matcher matcher = WORD_PATTERN.matcher(lower);

        List<String> tokens = new ArrayList<>();
        while (matcher.find()) {
            tokens.add(matcher.group());
        }
        return Collections.unmodifiableList(tokens);
    }

    // -------------------------------------------------------------------------
    // main de verificacion rapida (puede eliminarse cuando los tests esten listos)
    // -------------------------------------------------------------------------

    /**
     * Verificacion manual del contrato.
     * Ejecutar con: {@code mvn exec:java -Dexec.mainClass="com.thescratchers.searchengine.datamarts.TextTokenizer"}
     */
    public static void main(String[] args) {
        String input    = "Hello World, 123! Testing.";
        List<String> expected = List.of("hello", "world", "testing");
        List<String> result   = tokenize(input);

        System.out.println("Input   : \"" + input + "\"");
        System.out.println("Result  : " + result);
        System.out.println("Expected: " + expected);

        if (result.equals(expected)) {
            System.out.println("[OK] Tokenizer contract verified.");
        } else {
            System.err.println("[FAIL] Tokenizer output does not match expected!");
            System.exit(1);
        }
    }
}