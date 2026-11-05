#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${DS_TEST_DATABASE:?Create an empty ds_test_* database and set DS_TEST_DATABASE}"
python3 -m unittest distributed.tests.test_storage distributed.tests.test_integration -v
