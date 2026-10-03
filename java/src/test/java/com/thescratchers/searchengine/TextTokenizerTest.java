package com.thescratchers.searchengine.datamarts;

import org.junit.jupiter.api.Test;
import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.CsvSource;

import java.util.List;

import static org.junit.jupiter.api.Assertions.*;

class TextTokenizerTest {

    @Test
    void benchmarkReferenceCase() {
        List<String> result = TextTokenizer.tokenize("Hello World, 123! Testing.");
        assertEquals(List.of("hello", "world", "testing"), result);
    }

    @Test
    void convertsToLowerCase() {
        List<String> result = TextTokenizer.tokenize("UPPER lower MiXeD");
        assertEquals(List.of("upper", "lower", "mixed"), result);
    }

    @Test
    void discardsDigits() {
        List<String> result = TextTokenizer.tokenize("123 abc 456");
        assertEquals(List.of("abc"), result);
    }

    @Test
    void discardsPunctuation() {
        List<String> result = TextTokenizer.tokenize("hello! world? yes.");
        assertEquals(List.of("hello", "world", "yes"), result);
    }

    @Test
    void onlyDigitsReturnsEmpty() {
        assertTrue(TextTokenizer.tokenize("12345 6789").isEmpty());
    }

    @Test
    void noStemming() {
        List<String> result = TextTokenizer.tokenize("running");
        assertEquals(List.of("running"), result);
    }

    @Test
    void noStopwordRemoval() {
        List<String> result = TextTokenizer.tokenize("the cat is a mammal");
        assertEquals(List.of("the", "cat", "is", "a", "mammal"), result);
    }

    @Test
    void nullInputReturnsEmpty() {
        assertDoesNotThrow(() -> {
            List<String> result = TextTokenizer.tokenize(null);
            assertTrue(result.isEmpty());
        });
    }

    @Test
    void emptyStringReturnsEmpty() {
        assertTrue(TextTokenizer.tokenize("").isEmpty());
    }

    @Test
    void whitespaceAndSymbolsReturnsEmpty() {
        assertTrue(TextTokenizer.tokenize("   !@#$%^&*()   ").isEmpty());
    }

    @Test
    void noSpacesSingleToken() {
        List<String> result = TextTokenizer.tokenize("helloworld");
        assertEquals(List.of("helloworld"), result);
    }

    @ParameterizedTest
    @CsvSource({
        "Apple123,  apple",
        "42Book,    book",
        "test-case, test"
    })
    void firstTokenIsCorrect(String input, String expectedFirst) {
        List<String> result = TextTokenizer.tokenize(input);
        assertFalse(result.isEmpty());
        assertEquals(expectedFirst.trim(), result.get(0));
    }
}