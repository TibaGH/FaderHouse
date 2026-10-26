RNNoise v0.1.1 (https://github.com/xiph/rnnoise, tag v0.1.1, commit 6cbfd53), BSD-3-Clause, see COPYING.
Vendored for the Faderhouse Denoise plugin. Changes: variable length arrays in pitch.c and
celt_lpc.c replaced by alloca() so the code builds with MSVC. Only the files needed for the
library (with its built-in model, rnn_data.c) are included.
