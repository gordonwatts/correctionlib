from pathlib import Path

import numpy as np

import correctionlib

cset = correctionlib.CorrectionSet.from_file(
    str(Path(__file__).with_name("payload.json"))
)
x = np.array([0.25, 1.25, 0.5])
scalar_values = cset["scalar"].evaluate(x)  # (3,)
components = cset["pair"].evaluate(0.25)  # (2,)
rows = cset["pair"].evaluate(x)  # (3, 2)
np.testing.assert_array_equal(scalar_values, [1, 3, 1])
np.testing.assert_array_equal(components, [1, 2])
np.testing.assert_array_equal(rows, [[1, 2], [3, 4], [1, 2]])
print(scalar_values, components, rows, sep="\n")
