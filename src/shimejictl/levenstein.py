def lv_distance(s1: str, s2: str) -> float:
    len1, len2 = len(s1), len(s2)

    dp = [[0] * (len2 + 1) for _ in range(len1 + 1)]

    for i in range(len1 + 1):
        dp[i][0] = i
    for j in range(len2 + 1):
        dp[0][j] = j

    for i in range(1, len1 + 1):
        for j in range(1, len2 + 1):
            cost = 0 if s1[i - 1] == s2[j - 1] else 1
            dp[i][j] = min(dp[i - 1][j] + 1, dp[i][j - 1] + 1, dp[i - 1][j - 1] + cost)

    return dp[len1][len2]

def lv_variant(pool: list[str], s: str) -> str:
    best_match: str = None
    min_distance = float('inf')

    for variant in pool:
        distance = lv_distance(variant, s)
        if distance < min_distance:
            min_distance = distance
            best_match = variant

    return best_match
