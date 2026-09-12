using RogueSharp.DiceNotation;
using RogueSharp.Random;

namespace RogueGame;

public static class CombatSystem
{
    /// <summary>
    /// Rolls the attacker's damage dice, subtracts the defender's flat defense, floors at 1
    /// (an attack that connects always does *something*), and applies it. Returns the damage dealt.
    /// </summary>
    public static int ResolveAttack(Actor attacker, Actor defender, IRandom random)
    {
        int rolled = Dice.Roll(attacker.AttackDice, random);
        int damage = Math.Max(1, rolled - defender.Defense);
        defender.Health = Math.Max(0, defender.Health - damage);
        return damage;
    }
}
