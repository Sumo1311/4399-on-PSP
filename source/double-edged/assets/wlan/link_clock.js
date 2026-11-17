// Shared room epoch: original depth-order timestamps match on both PSPs.
// This configures the clock only; actor/game fields are not assigned here.
(function(global){
    var NativeDate=global.Date;
    function FixedDate(a,b,c,d,e,f,g){
        if(!(this instanceof FixedDate))return new NativeDate(0).toString();
        switch(arguments.length){case 0:return new NativeDate(0);case 1:return new NativeDate(a);case 2:return new NativeDate(a,b);case 3:return new NativeDate(a,b,c);case 4:return new NativeDate(a,b,c,d);case 5:return new NativeDate(a,b,c,d,e);case 6:return new NativeDate(a,b,c,d,e,f);default:return new NativeDate(a,b,c,d,e,f,g);}
    }
    FixedDate.prototype=NativeDate.prototype;FixedDate.now=function(){return 0;};FixedDate.parse=NativeDate.parse;FixedDate.UTC=NativeDate.UTC;global.Date=FixedDate;
})(this);
