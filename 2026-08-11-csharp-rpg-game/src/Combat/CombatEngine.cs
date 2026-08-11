using RpgGame.Models;

namespace RpgGame.Combat;

public sealed record CombatRoundResult(int PlayerDamageDealt, int? MonsterDamageDealt, bool MonsterDefeated, bool PlayerDefeated);

public sealed class CombatEngine
{
    private readonly IRandomSource _random;

    public CombatEngine(IRandomSource random)
    {
        _random = random;
    }

    // One round: the player always swings first. If that swing finishes the
    // monster off, the monster doesn't get a retaliation hit -- a dead
    // monster doesn't get to attack back, so MonsterDamageDealt is null
    // rather than 0 in that case (0 would mean "attacked and missed", which
    // isn't what happened).
    public CombatRoundResult ExecuteRound(Player player, Monster monster)
    {
        if (!player.IsAlive) throw new InvalidOperationException("Player is already defeated.");
        if (!monster.IsAlive) throw new InvalidOperationException("Monster is already defeated.");

        var playerDamage = _random.Next(player.AttackMinDamage, player.AttackMaxDamage);
        monster.CurrentHitPoints = Math.Max(0, monster.CurrentHitPoints - playerDamage);

        if (!monster.IsAlive)
        {
            return new CombatRoundResult(playerDamage, null, MonsterDefeated: true, PlayerDefeated: false);
        }

        var monsterDamage = _random.Next(monster.MinDamage, monster.MaxDamage);
        player.TakeDamage(monsterDamage);

        return new CombatRoundResult(playerDamage, monsterDamage, MonsterDefeated: false, PlayerDefeated: !player.IsAlive);
    }

    // A fled fight still costs you: the monster gets one free, unanswered
    // hit on the way out. Returns the damage dealt, or null if the player
    // died to the parting shot.
    public int? MonsterFreeAttack(Player player, Monster monster)
    {
        var damage = _random.Next(monster.MinDamage, monster.MaxDamage);
        player.TakeDamage(damage);
        return damage;
    }

    public bool RollFleeSuccess() => _random.Next(1, 100) <= 50;

    // Guaranteed entries skip the roll entirely -- they don't consume a
    // draw from the random sequence. That matters for tests: a fixed
    // sequence like [7, 3] against a monster with one guaranteed drop and
    // one 50% drop is consumed as a single roll (the guaranteed one costs
    // nothing), not two.
    public List<Item> RollLoot(Monster monster)
    {
        var loot = new List<Item>();
        foreach (var entry in monster.LootTable)
        {
            if (entry.IsGuaranteed || _random.Next(1, 100) <= entry.DropChancePercent)
            {
                loot.Add(entry.Item);
            }
        }

        return loot;
    }
}
