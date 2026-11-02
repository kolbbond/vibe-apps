// FFT input window for vvavviz (header-only, no GUI/audio deps, so it is unit tested)
#pragma once

#include <armadillo>

// the newest `bufsize` columns of `mono`, zero-padded in front when it is shorter (the tail of a file):
// the FFT then always sees one length, so its bins and plan stay the same to the last frame
template<typename T>
arma::Mat<T> fft_window(const arma::Mat<T>& mono, arma::uword bufsize) {
    if (mono.n_cols >= bufsize) return mono.cols(mono.n_cols - bufsize, mono.n_cols - 1);
    arma::Mat<T> window(mono.n_rows, bufsize, arma::fill::zeros);
    if (mono.n_cols > 0) window.cols(bufsize - mono.n_cols, bufsize - 1) = mono;
    return window;
}
