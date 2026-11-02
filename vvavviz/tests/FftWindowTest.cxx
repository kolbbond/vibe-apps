#include <Corrade/TestSuite/Tester.h>

#include "FftWindow.hxx"

struct FftWindowTest : Corrade::TestSuite::Tester {
    explicit FftWindowTest();

    void longInputKeepsNewest();
    void exactLengthUnchanged();
    void shortTailIsZeroPaddedInFront();
    void emptyInputIsSilence();
};

FftWindowTest::FftWindowTest() {
    addTests({&FftWindowTest::longInputKeepsNewest,
        &FftWindowTest::exactLengthUnchanged,
        &FftWindowTest::shortTailIsZeroPaddedInFront,
        &FftWindowTest::emptyInputIsSilence});
}

void FftWindowTest::longInputKeepsNewest() {
    const arma::mat mono = arma::regspace<arma::rowvec>(1, 10);
    const arma::mat w = fft_window(mono, 4);
    CORRADE_COMPARE(w.n_cols, 4);
    CORRADE_COMPARE(w(0, 0), 7.0);
    CORRADE_COMPARE(w(0, 3), 10.0);
}

void FftWindowTest::exactLengthUnchanged() {
    const arma::mat mono = arma::regspace<arma::rowvec>(1, 4);
    CORRADE_VERIFY(arma::approx_equal(fft_window(mono, 4), mono, "absdiff", 0.0));
}

void FftWindowTest::shortTailIsZeroPaddedInFront() {
    // the last 3 samples of a track with a 1024 buffer: still 1024 long, samples at the end
    const arma::mat mono = {{0.5, -0.5, 0.25}};
    const arma::mat w = fft_window(mono, 1024);
    CORRADE_COMPARE(w.n_rows, 1);
    CORRADE_COMPARE(w.n_cols, 1024);
    CORRADE_COMPARE(arma::accu(arma::abs(w.cols(0, 1020))), 0.0);
    CORRADE_COMPARE(w(0, 1021), 0.5);
    CORRADE_COMPARE(w(0, 1023), 0.25);
}

void FftWindowTest::emptyInputIsSilence() {
    const arma::mat w = fft_window(arma::mat(1, 0), 8);
    CORRADE_COMPARE(w.n_cols, 8);
    CORRADE_COMPARE(arma::accu(arma::abs(w)), 0.0);
}

CORRADE_TEST_MAIN(FftWindowTest)
