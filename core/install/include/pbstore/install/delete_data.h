#pragma once
// Deleting the data folder of an app is a separate step after a completed removal, never part of it. It needs
// two confirmations that differ in wording and in what they ask for: the name of the folder, then the number of
// files that will go. It never follows links. An interruption leaves a partly deleted folder; running the step
// again finishes it (nothing is ever restored, so no journal is needed).

#include <optional>
#include <string>
#include <vector>

#include "pbstore/install/plan.h"
#include "pbstore/install/progress.h"
#include "pbstore/install/reader_state.h"
#include "pbstore/rules.h"

namespace pbstore {
namespace install {

struct DeleteDataPlanResult {
    std::optional<Plan> plan;
    std::vector<Finding> findings;   // E-INS-16 (shared with another app), E-INS-18, E-INS-22, E-INS-19
    std::vector<Finding> notices;    // the app has no data folder, or it is already gone
};

// `data_dir` is the folder named by the removal's plan (the record is gone by then). The snapshot must have
// been scanned with ScanRequest::data_dir set to it.
DeleteDataPlanResult plan_delete_data(const std::string& app_id, const std::optional<std::string>& data_dir, const ReaderSnapshot& snapshot);

// The two confirmations: what each asks for, and the words used by every front end.
std::string delete_data_token_1(const Plan& plan);      // the folder as shown: system/<name>
std::string delete_data_token_2(const Plan& plan);      // the number of files
std::string delete_data_question_1(const Plan& plan);
std::string delete_data_question_2(const Plan& plan);

struct DeleteResult {
    bool done = false;
    bool cancelled = false;
    bool wrong_token_1 = false, wrong_token_2 = false;
    uint64_t files_deleted = 0;
    std::vector<Finding> findings;   // E-INS-25 (the folder changed since the plan), E-INS-34, E-INS-37
};

// Deletes the folder if both tokens are right and the folder is as planned; otherwise changes nothing.
DeleteResult delete_data(ReaderFs& fs, const Plan& plan, const std::string& token1, const std::string& token2, const ProgressFn& progress,
                         CancelToken& cancel);

}  // namespace install
}  // namespace pbstore
