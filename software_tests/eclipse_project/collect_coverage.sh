#! /bin/bash
# Script to collect coverage results and produce an HTML report.
# Run after an arbitrary number of test programs compiled for coverage have been run.

# Get the absolute path of this script.
SCRIPT=$(readlink -f $0)
SCRIPT_PATH=`dirname ${SCRIPT}`

# Collect the coverage results
RESULTS_DIR=${SCRIPT_PATH}/bin/coverage_results
rm -rf ${RESULTS_DIR}
mkdir -p ${RESULTS_DIR}
lcov -d ${SCRIPT_PATH} -c -o ${RESULTS_DIR}/lcov.trace --rc lcov_branch_coverage=1 --rc geninfo_unexecuted_blocks=1 > ${RESULTS_DIR}/report.log

# Generate HTML report
genhtml -o ${RESULTS_DIR} ${RESULTS_DIR}/lcov.trace --branch-coverage >> ${RESULTS_DIR}/report.log

# Delete coverage results, in preperation for next text
find ${SCRIPT_PATH} -name '*.gcda' | xargs rm
