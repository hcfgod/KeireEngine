@{
    # These rules conflict with intentional interactive CLI output and internal
    # helper naming. Correctness and security diagnostics remain enabled.
    ExcludeRules = @(
        'PSAvoidUsingWriteHost'
        'PSReviewUnusedParameter'
        'PSUseApprovedVerbs'
        'PSUseShouldProcessForStateChangingFunctions'
        'PSUseSingularNouns'
    )
}
