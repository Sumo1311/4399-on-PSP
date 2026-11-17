// User-selected visual simplifications. Original class extraction stays intact.
(function () {
    var Coin = DoubleEdged_fla.coin_spin_233;
    if (!Coin || !Coin.prototype.frame1 || !Coin.prototype.frame15)
        throw new Error('Original coin animation class is unavailable');
    var first = Coin.prototype.frame1, loop = Coin.prototype.frame15;
    Coin.prototype.frame1 = function () {
        // Keep the original random draw and initial script side effects. While
        // this frame script runs, the final queued goto selects the full front face (frame 8).
        first.call(this);
        this.gotoAndStop(8);
    };
    Coin.prototype.frame15 = function () {
        loop.call(this);
        this.gotoAndStop(8);
    };
}());
