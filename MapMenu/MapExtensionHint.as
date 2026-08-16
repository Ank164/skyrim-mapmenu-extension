import gfx.io.GameDelegate;
import gfx.managers.FocusHandler;

class MapExtensionHint extends MovieClip
{
	#include "../version.as"
	
	var HintHolder_mc:MovieClip;
	var HintHolder:MovieClip;
	
	function MapExtensionHint()
	{
		super();
		HintHolder = HintHolder_mc;
		HintHolder.hintText.textAutoSize = "shrink";
		
		this._visible = true;
	}
	
	function getWidth()
	{
		return this._width;
	}
	function getHeight()
	{
		return this._height;
	}
	function setVisible(a_visible)
	{
		this._visible = a_visible;
	}
	function setPosition(a_x, a_y)
	{
		this._x = a_x;
		this._y = a_y;
	}
	function setScale(a_scale:Number)
	{
		this._width = this._width * a_scale;
		this._height = this._height * a_scale;
	}
	
	function setButton(gamepad, menuHotkey, menuGamepadHotkey)
	{
		if (!gamepad){
			HintHolder.button.gotoAndStop(menuHotkey);
		}else{
			HintHolder.button.gotoAndStop(menuGamepadHotkey);
		}
	}
	
	function setWidescreen(widescreen:Boolean):Void {
		if(!widescreen){
			return;
		}
		
		HintHolder._x += 200;
	}
}