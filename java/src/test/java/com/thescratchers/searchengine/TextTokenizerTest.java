package com.thescratchers.searchengine.datamarts;

import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

/**
 * Tests del contrato estricto de {@link TextTokenizer}.
 *
 * <p>Estos casos de prueba actuan como especificacion ejecutable compartida
 * con las implementaciones en otros lenguajes del benchmark.</p>
 */
@DisplayName("TextTokenizer – contrato de tokenizacion")
class TextTokenizerTest {

    // ── Caso de referencia del benchmark ─────────────────────────────────────

    @Test
    @DisplayName("Caso benchmark: 'Hello World, 123! Testing.' => [hello, world, testing]")
    void benchmarkReferenceCase() {
        List<String> result = TextTokenizer.tokenize("Hello World, 123! Testing.");
        assertEquals(List.of("hello", "world", "testing"), result);
    }

    // ── Conversion a minusculas ───────────────────────────────────────────────

    @Test
    @DisplayName("Convierte a minusculas correctamente")
    void convertsToLowerCase() {
        List<String> result = TextTokenizer.tokenize("UPPER lower MiXeD");
        assertEquals(List.of("upper", "lower", "mixed"), result);
    }

    // ── Exclusion de digitos y puntuacion ────────────────────────────────────

    @Test
    @DisplayName("Descarta digitos: '123 abc 456' => [abc]")
    void discardsDigits() {
        List<String> result = TextTokenizer.tokenize("123 abc 456");
        assertEquals(List.of("abc"), result);
    }

    @Test
    @DisplayName("Descarta puntuacion y simbolos")
    void discardsPunctuation() {
        List<String> result = TextTokenizer.tokenize("hello! world? yes.");
        assertEquals(List.of("hello", "world", "yes"), result);
    }

    @Test
    @DisplayName("Texto solo con digitos devuelve lista vacia")
    void onlyDigitsReturnsEmpty() {
        assertTrue(TextTokenizer.tokenize("12345 6789").isEmpty());
    }

    // ── Sin stemming ni stopwords ─────────────────────────────────────────────

    @Test
    @DisplayName("No aplica stemming: 'running' no se convierte en 'run'")
    void noStemming() {
        List<String> result = TextTokenizer.tokenize("running");
        assertEquals(List.of("running"), result);
    }

    @Test
    @DisplayName("No elimina stopwords: 'the', 'is', 'a' se mantienen")
    void noStopwordRemoval() {
        List<String> result = TextTokenizer.tokenize("the cat is a mammal");
        assertEquals(List.of("the", "cat", "is", "a", "mammal"), result);
    }

    // ── Casos limite ─────────────────────────────────────────────────────────

    @Test
    @DisplayName("null devuelve lista vacia (sin NullPointerException)")
    void nullInputReturnsEmpty() {
        assertDoesNotThrow(() -> {
            List<String> result = TextTokenizer.tokenize(null);
            assertTrue(result.isEmpty());
        });
    }

    @Test
    @DisplayName("String vacio devuelve lista vacia")
    void emptyStringReturnsEmpty() {
        assertTrue(TextTokenizer.tokenize("").isEmpty());
    }

    @Test
    @DisplayName("Texto con solo espacios y simbolos devuelve lista vacia")
    void whitespaceAndSymbolsReturnsEmpty() {
        assertTrue(TextTokenizer.tokenize("   !@#$%^&*()   ").isEmpty());
    }

    // ── Texto continuo (sin espacios) ────────────────────────────────────────

    @Test
    @DisplayName("Texto sin espacios se trata como un solo token")
    void noSpacesSingleToken() {
        List<String> result = TextTokenizer.tokenize("helloworld");
        assertEquals(List.of("helloworld"), result);
    }

    // ── Parametrizado: verificacion de tokens individuales ───────────────────

    @ParameterizedTest(name = "''{0}'' => primer token ''{1}''")
    @DisplayName("Primer token extraido correctamente")
    @CsvSource({
        "Apple123,  apple",
        "42Book,    book",
        "test-case, test"
    })
    void firstTokenIsCorrect(String input, String expectedFirst) {
        List<String> result = TextTokenizer.tokenize(input);
        assertFalse(result.isEmpty(), "Expected at least one token for input: " + input);
        assertEquals(expectedFirst.trim(), result.get(0));
    }
}