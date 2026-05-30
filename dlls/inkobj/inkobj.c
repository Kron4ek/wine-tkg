/* Copyright (C) 2007 C John Klehm
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "inkobj_internal.h"

WINE_DEFAULT_DEBUG_CHANNEL(inkobj);

struct ink_disp
{
    IInkDisp IInkDisp_iface;
    LONG ref;
};

struct inkoverlay
{
    IInkOverlay IInkOverlay_iface;
    LONG ref;
};

static inline struct ink_disp *impl_from_IInkDisp( IInkDisp *iface )
{
    return CONTAINING_RECORD( iface, struct ink_disp, IInkDisp_iface );
}

static inline struct inkoverlay *impl_from_IInkOverlay( IInkOverlay *iface )
{
    return CONTAINING_RECORD( iface, struct inkoverlay, IInkOverlay_iface );
}


/*** IUnknown methods ***/
static HRESULT WINAPI inkdisp_QueryInterface(IInkDisp *iface, REFIID riid, void **obj)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );
    TRACE( "%p, %s, %p\n", iface, debugstr_guid(riid), obj );

    *obj = NULL;

    if (IsEqualIID(riid, &IID_IUnknown)  ||
        IsEqualIID(riid, &IID_IDispatch) ||
        IsEqualIID(riid, &IID_IInkDisp))
    {
        *obj = &ink_disp->IInkDisp_iface;
    }
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown*)*obj );
    return S_OK;
}


static ULONG WINAPI inkdisp_AddRef(IInkDisp *iface)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );
    return InterlockedIncrement( &ink_disp->ref );
}

static ULONG WINAPI inkdisp_Release(IInkDisp *iface)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );
    LONG ref = InterlockedDecrement( &ink_disp->ref );
    if (!ref)
    {
        free( ink_disp );
    }
    return ref;
}

static HRESULT WINAPI inkdisp_GetTypeInfoCount(IInkDisp *iface, UINT *pctinfo)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, pctinfo);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_GetTypeInfo(IInkDisp *iface, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    TRACE("%p, %u, %lx, %p.\n", ink_disp, iTInfo, lcid, ppTInfo);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_GetIDsOfNames(IInkDisp *iface, REFIID riid, LPOLESTR *rgszNames, UINT cNames,
    LCID lcid, DISPID *rgDispId)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    TRACE("%p, %s, %p, %u, %lx, %p.\n", ink_disp, debugstr_guid(riid), rgszNames, cNames, lcid, rgDispId);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_Invoke(IInkDisp *iface, DISPID dispIdMember, REFIID riid, LCID lcid, WORD wFlags,
    DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    TRACE("%p, %ld, %s, %lx, %d, %p, %p, %p, %p.\n", ink_disp, dispIdMember, debugstr_guid(riid),
          lcid, wFlags, pDispParams, pVarResult, pExcepInfo, puArgErr);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_get_Strokes(IInkDisp *iface, IInkStrokes **strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_get_ExtendedProperties(IInkDisp *iface, IInkExtendedProperties **properties)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, properties);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_get_Dirty(IInkDisp *iface, VARIANT_BOOL *dirty)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, dirty);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_put_Dirty(IInkDisp *iface, VARIANT_BOOL Dirty)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %d\n", ink_disp, Dirty);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_get_CustomStrokes(IInkDisp *iface, IInkCustomStrokes **strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_GetBoundingBox(IInkDisp *iface, InkBoundingBoxMode mode, IInkRectangle **rect)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %d, %p\n", ink_disp, mode, rect);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_DeleteStrokes(IInkDisp *iface, IInkStrokes *strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_DeleteStroke(IInkDisp *iface, IInkStrokeDisp *strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_ExtractStrokes(IInkDisp *iface, IInkStrokes *strokes, InkExtractFlags flags,
            IInkDisp **ink)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %d, %p\n", ink_disp, flags, ink);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_ExtractWithRectangle(IInkDisp *iface, IInkRectangle *rectangle, InkExtractFlags flags,
           IInkDisp **ink)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p, %d, %p\n", ink_disp, rectangle, flags, ink);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_Clip(IInkDisp *iface, IInkRectangle *rectangle)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, rectangle);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_Clone(IInkDisp *iface, IInkDisp **ink)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p\n", ink_disp, ink);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_HitTestCircle(IInkDisp *iface, LONG x, LONG y, float radius, IInkStrokes **strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %ld, %ld, %f, %p\n", ink_disp, x, y, radius, strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_HitTestWithRectangle(IInkDisp *iface, IInkRectangle *rectangle,
            float IntersectPercent, IInkStrokes **strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p, %f, %p\n", ink_disp, rectangle, IntersectPercent, strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_HitTestWithLasso(IInkDisp *iface, VARIANT points, float percent,
    VARIANT *lasso, IInkStrokes **strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %s, %f, %p, %p\n", ink_disp, debugstr_variant(&points), percent, lasso, strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_NearestPoint(IInkDisp *iface, LONG x, LONG y, float *point_on,
    float *distance, IInkStrokeDisp **stroke)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %ld, %ld, %p, %p, %p\n", ink_disp, x, y, point_on, distance, stroke );

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_CreateStrokes(IInkDisp *iface, VARIANT ids, IInkStrokes **strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %s, %p\n", ink_disp, debugstr_variant(&ids), strokes);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_AddStrokesAtRectangle(IInkDisp *iface, IInkStrokes *strokes, IInkRectangle *rect)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p, %p\n", ink_disp, strokes, rect);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_Save(IInkDisp *iface, InkPersistenceFormat persistence,
    InkPersistenceCompressionMode mode, VARIANT *data)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %d, %d, %p\n", ink_disp, persistence, mode, data);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_Load(IInkDisp *iface, VARIANT data)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %s\n", ink_disp, debugstr_variant(&data));

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_CreateStroke(IInkDisp *iface, VARIANT data, VARIANT description, IInkStrokeDisp **stroke)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %s, %s, %p\n", ink_disp, debugstr_variant(&data), debugstr_variant(&description), stroke);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_ClipboardCopyWithRectangle(IInkDisp *iface, IInkRectangle *Rectangle, InkClipboardFormats ClipboardFormats,
    InkClipboardModes ClipboardModes, /*IDataObject*/ IUnknown **DataObject)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p, %d, %d, %p\n", ink_disp, Rectangle, ClipboardFormats, ClipboardModes, DataObject);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_ClipboardCopy(IInkDisp *iface, IInkStrokes *strokes, InkClipboardFormats ClipboardFormats,
    InkClipboardModes ClipboardModes, /*IDataObject*/ IUnknown **DataObject)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p, %d, %d, %p\n", ink_disp, strokes, ClipboardFormats, ClipboardModes, DataObject);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_CanPaste(IInkDisp *iface, /*IDataObject*/ IUnknown *DataObject, VARIANT_BOOL *CanPaste)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p, %p, %p\n", ink_disp, DataObject, CanPaste);

    return E_NOTIMPL;
}

static HRESULT WINAPI inkdisp_ClipboardPaste(IInkDisp *iface, LONG x, LONG y, /*IDataObject*/ IUnknown *DataObject, IInkStrokes **Strokes)
{
    struct ink_disp *ink_disp = impl_from_IInkDisp( iface );

    FIXME("%p\n", ink_disp);

    return E_NOTIMPL;
}

static const struct IInkDispVtbl ink_disp_vtbl =
{
    inkdisp_QueryInterface,
    inkdisp_AddRef,
    inkdisp_Release,
    inkdisp_GetTypeInfoCount,
    inkdisp_GetTypeInfo,
    inkdisp_GetIDsOfNames,
    inkdisp_Invoke,
    inkdisp_get_Strokes,
    inkdisp_get_ExtendedProperties,
    inkdisp_get_Dirty,
    inkdisp_put_Dirty,
    inkdisp_get_CustomStrokes,
    inkdisp_GetBoundingBox,
    inkdisp_DeleteStrokes,
    inkdisp_DeleteStroke,
    inkdisp_ExtractStrokes,
    inkdisp_ExtractWithRectangle,
    inkdisp_Clip,
    inkdisp_Clone,
    inkdisp_HitTestCircle,
    inkdisp_HitTestWithRectangle,
    inkdisp_HitTestWithLasso,
    inkdisp_NearestPoint,
    inkdisp_CreateStrokes,
    inkdisp_AddStrokesAtRectangle,
    inkdisp_Save,
    inkdisp_Load,
    inkdisp_CreateStroke,
    inkdisp_ClipboardCopyWithRectangle,
    inkdisp_ClipboardCopy,
    inkdisp_CanPaste,
    inkdisp_ClipboardPaste
};

static HRESULT WINAPI inkoverlay_QueryInterface(IInkOverlay *iface, REFIID riid, void **obj)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );
    TRACE( "%p, %s, %p\n", iface, debugstr_guid(riid), obj );

    *obj = NULL;

    if (IsEqualIID(riid, &IID_IUnknown)  ||
        IsEqualIID(riid, &IID_IDispatch) ||
        IsEqualIID(riid, &IID_IInkOverlay))
    {
        *obj = &inkoverlay->IInkOverlay_iface;
    }
    else
    {
        FIXME( "interface %s not implemented\n", debugstr_guid(riid) );
        return E_NOINTERFACE;
    }

    IUnknown_AddRef( (IUnknown*)*obj );
    return S_OK;
}

static ULONG WINAPI inkoverlay_AddRef(IInkOverlay *iface)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );
    return InterlockedIncrement( &inkoverlay->ref );
}

static ULONG WINAPI inkoverlay_Release(IInkOverlay *iface)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );
    LONG ref = InterlockedDecrement( &inkoverlay->ref );
    if (!ref)
    {
        free( inkoverlay );
    }
    return ref;
}

static HRESULT WINAPI inkoverlay_GetTypeInfoCount(IInkOverlay *iface, UINT *pctinfo)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_GetTypeInfo(IInkOverlay *iface, UINT iTInfo, LCID lcid, ITypeInfo **ppTInfo)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_GetIDsOfNames(IInkOverlay *iface, REFIID riid, LPOLESTR *rgszNames, UINT cNames,
    LCID lcid, DISPID *rgDispId)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_Invoke(IInkOverlay *iface, DISPID dispIdMember, REFIID riid, LCID lcid,
    WORD wFlags, DISPPARAMS *pDispParams, VARIANT *pVarResult, EXCEPINFO *pExcepInfo, UINT *puArgErr)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_hWnd(IInkOverlay *iface, LONG_PTR *window)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_hWnd(IInkOverlay *iface, LONG_PTR window)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_Enabled( IInkOverlay *iface, VARIANT_BOOL *enabled)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    *enabled = VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI inkoverlay_put_Enabled(IInkOverlay *iface, VARIANT_BOOL enabled)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_DefaultDrawingAttributes(IInkOverlay *iface, IInkDrawingAttributes **attributes)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_putref_DefaultDrawingAttributes(IInkOverlay *iface, IInkDrawingAttributes *attributes)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_Renderer( IInkOverlay *iface, IInkRenderer **CurrentInkRenderer)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_putref_Renderer( IInkOverlay *iface, IInkRenderer *CurrentInkRenderer)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_Ink(IInkOverlay *iface, IInkDisp **Ink)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    *Ink = NULL;
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_putref_Ink( IInkOverlay *iface, IInkDisp *Ink)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_AutoRedraw(IInkOverlay *iface, VARIANT_BOOL *AutoRedraw)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_AutoRedraw( IInkOverlay *iface, VARIANT_BOOL AutoRedraw)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_CollectingInk(IInkOverlay *iface, VARIANT_BOOL *collecting)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);

    *collecting = VARIANT_FALSE;
    return S_OK;
}

static HRESULT WINAPI inkoverlay_get_CollectionMode( IInkOverlay *iface, InkCollectionMode *mode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_CollectionMode(IInkOverlay *iface, InkCollectionMode Mode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_DynamicRendering( IInkOverlay *iface, VARIANT_BOOL *Enabled)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_DynamicRendering( IInkOverlay *iface, VARIANT_BOOL enabled)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_DesiredPacketDescription(IInkOverlay *iface, VARIANT *PacketGuids)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_DesiredPacketDescription(IInkOverlay *iface, VARIANT PacketGuids)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_MouseIcon(IInkOverlay *iface, IPictureDisp **MouseIcon)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_MouseIcon(IInkOverlay *iface, IPictureDisp *MouseIcon)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_putref_MouseIcon(IInkOverlay *iface, IPictureDisp *MouseIcon)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_MousePointer(IInkOverlay *iface, InkMousePointer *MousePointer)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_MousePointer(IInkOverlay *iface, InkMousePointer MousePointer)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_EditingMode(IInkOverlay *iface, InkOverlayEditingMode *EditingMode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_EditingMode(IInkOverlay *iface, InkOverlayEditingMode EditingMode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_Selection(IInkOverlay *iface, IInkStrokes **Selection)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_Selection(IInkOverlay *iface, IInkStrokes *Selection)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_EraserMode(IInkOverlay *iface, InkOverlayEraserMode *EraserMode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_EraserMode(IInkOverlay *iface, InkOverlayEraserMode EraserMode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_EraserWidth(IInkOverlay *iface, LONG *EraserWidth)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_EraserWidth(IInkOverlay *iface, LONG EraserWidth)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_AttachMode(IInkOverlay *iface, InkOverlayAttachMode *AttachMode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_AttachMode(IInkOverlay *iface, InkOverlayAttachMode AttachMode)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_Cursors(IInkOverlay *iface, IInkCursors **Cursors)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_MarginX(IInkOverlay *iface, LONG *MarginX)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_MarginX(IInkOverlay *iface, LONG MarginX)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_MarginY(IInkOverlay *iface, LONG *MarginY)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_MarginY(IInkOverlay *iface, LONG MarginY)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_Tablet(IInkOverlay *iface, IInkTablet **SingleTablet)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_SupportHighContrastInk(IInkOverlay *iface, VARIANT_BOOL *Support)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_SupportHighContrastInk(IInkOverlay *iface, VARIANT_BOOL Support)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_get_SupportHighContrastSelectionUI(IInkOverlay *iface, VARIANT_BOOL *Support)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_put_SupportHighContrastSelectionUI(IInkOverlay *iface, VARIANT_BOOL Support)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_HitTestSelection(IInkOverlay *iface, LONG x, LONG y, SelectionHitResult *SelArea)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_Draw(IInkOverlay *iface, IInkRectangle *Rect)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_SetGestureStatus(IInkOverlay *iface, InkApplicationGesture Gesture, VARIANT_BOOL Listen)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_GetGestureStatus(IInkOverlay *iface, InkApplicationGesture Gesture, VARIANT_BOOL *Listening)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_GetWindowInputRectangle(IInkOverlay *iface, IInkRectangle **WindowInputRectangle)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_SetWindowInputRectangle(IInkOverlay *iface, IInkRectangle *WindowInputRectangle)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_SetAllTabletsMode(IInkOverlay *iface, VARIANT_BOOL UseMouseForInput)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_SetSingleTabletIntegratedMode(IInkOverlay *iface, IInkTablet *Tablet)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_GetEventInterest(IInkOverlay *iface, InkCollectorEventInterest EventId, VARIANT_BOOL *Listen)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static HRESULT WINAPI inkoverlay_SetEventInterest(IInkOverlay *iface, InkCollectorEventInterest EventId, VARIANT_BOOL Listen)
{
    struct inkoverlay *inkoverlay = impl_from_IInkOverlay( iface );

    FIXME("%p\n", inkoverlay);
    return E_NOTIMPL;
}

static struct IInkOverlayVtbl inkoverlay_vtbl =
{
    inkoverlay_QueryInterface,
    inkoverlay_AddRef,
    inkoverlay_Release,
    inkoverlay_GetTypeInfoCount,
    inkoverlay_GetTypeInfo,
    inkoverlay_GetIDsOfNames,
    inkoverlay_Invoke,
    inkoverlay_get_hWnd,
    inkoverlay_put_hWnd,
    inkoverlay_get_Enabled,
    inkoverlay_put_Enabled,
    inkoverlay_get_DefaultDrawingAttributes,
    inkoverlay_putref_DefaultDrawingAttributes,
    inkoverlay_get_Renderer,
    inkoverlay_putref_Renderer,
    inkoverlay_get_Ink,
    inkoverlay_putref_Ink,
    inkoverlay_get_AutoRedraw,
    inkoverlay_put_AutoRedraw,
    inkoverlay_get_CollectingInk,
    inkoverlay_get_CollectionMode,
    inkoverlay_put_CollectionMode,
    inkoverlay_get_DynamicRendering,
    inkoverlay_put_DynamicRendering,
    inkoverlay_get_DesiredPacketDescription,
    inkoverlay_put_DesiredPacketDescription,
    inkoverlay_get_MouseIcon,
    inkoverlay_put_MouseIcon,
    inkoverlay_putref_MouseIcon,
    inkoverlay_get_MousePointer,
    inkoverlay_put_MousePointer,
    inkoverlay_get_EditingMode,
    inkoverlay_put_EditingMode,
    inkoverlay_get_Selection,
    inkoverlay_put_Selection,
    inkoverlay_get_EraserMode,
    inkoverlay_put_EraserMode,
    inkoverlay_get_EraserWidth,
    inkoverlay_put_EraserWidth,
    inkoverlay_get_AttachMode,
    inkoverlay_put_AttachMode,
    inkoverlay_get_Cursors,
    inkoverlay_get_MarginX,
    inkoverlay_put_MarginX,
    inkoverlay_get_MarginY,
    inkoverlay_put_MarginY,
    inkoverlay_get_Tablet,
    inkoverlay_get_SupportHighContrastInk,
    inkoverlay_put_SupportHighContrastInk,
    inkoverlay_get_SupportHighContrastSelectionUI,
    inkoverlay_put_SupportHighContrastSelectionUI,
    inkoverlay_HitTestSelection,
    inkoverlay_Draw,
    inkoverlay_SetGestureStatus,
    inkoverlay_GetGestureStatus,
    inkoverlay_GetWindowInputRectangle,
    inkoverlay_SetWindowInputRectangle,
    inkoverlay_SetAllTabletsMode,
    inkoverlay_SetSingleTabletIntegratedMode,
    inkoverlay_GetEventInterest,
    inkoverlay_SetEventInterest
};

HRESULT WINAPI inkdisp_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    struct ink_disp *disp;

    TRACE("(%p %s %p)\n", outer, debugstr_guid(riid), ppv);

    if (!(disp = malloc( sizeof(*disp) ))) return E_OUTOFMEMORY;
    disp->IInkDisp_iface.lpVtbl = &ink_disp_vtbl;
    disp->ref = 1;

    *ppv = &disp->IInkDisp_iface;
    TRACE( "returning iface %p\n", *ppv );
    return S_OK;
}

HRESULT WINAPI inkoverlay_CreateInstance(IClassFactory *iface, IUnknown *outer, REFIID riid, void **ppv)
{
    struct inkoverlay *overlay;

    TRACE("(%p %s %p)\n", outer, debugstr_guid(riid), ppv);

    if (!(overlay = malloc( sizeof(*overlay) ))) return E_OUTOFMEMORY;
    overlay->IInkOverlay_iface.lpVtbl = &inkoverlay_vtbl;
    overlay->ref = 1;

    *ppv = &overlay->IInkOverlay_iface;
    TRACE( "returning iface %p\n", *ppv );
    return S_OK;
}

static HRESULT WINAPI ClassFactory_QueryInterface(IClassFactory *iface, REFIID riid, void **ppv)
{
    *ppv = NULL;

    if(IsEqualGUID(&IID_IUnknown, riid)) {
        TRACE("(%p)->(IID_IUnknown %p)\n", iface, ppv);
        *ppv = iface;
    }else if(IsEqualGUID(&IID_IClassFactory, riid)) {
        TRACE("(%p)->(IID_IClassFactory %p)\n", iface, ppv);
        *ppv = iface;
    }

    if(*ppv) {
        IUnknown_AddRef((IUnknown*)*ppv);
        return S_OK;
    }

    WARN("(%p)->(%s %p)\n", iface, debugstr_guid(riid), ppv);
    return E_NOINTERFACE;
}

static ULONG WINAPI ClassFactory_AddRef(IClassFactory *iface)
{
    TRACE("(%p)\n", iface);
    return 2;
}

static ULONG WINAPI ClassFactory_Release(IClassFactory *iface)
{
    TRACE("(%p)\n", iface);
    return 1;
}



static HRESULT WINAPI ClassFactory_LockServer(IClassFactory *iface, BOOL fLock)
{
    TRACE("(%p)->(%x)\n", iface, fLock);
    return S_OK;
}

static const IClassFactoryVtbl cfinkdisplVtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    inkdisp_CreateInstance,
    ClassFactory_LockServer
};

static const IClassFactoryVtbl cfinkoverlayVtbl = {
    ClassFactory_QueryInterface,
    ClassFactory_AddRef,
    ClassFactory_Release,
    inkoverlay_CreateInstance,
    ClassFactory_LockServer
};

static IClassFactory cf_inkdisp = { &cfinkdisplVtbl };
static IClassFactory cf_overlay = { &cfinkoverlayVtbl };

/*****************************************************
 *    DllGetClassObject [INKOBJ.@]
 */
HRESULT WINAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
    TRACE("%s %s %p\n", debugstr_guid(rclsid), debugstr_guid(riid), ppv);

    if (IsEqualGUID(&CLSID_InkDisp, rclsid))
    {
        return IClassFactory_QueryInterface(&cf_inkdisp, riid, ppv);
    }
    else if (IsEqualGUID(&CLSID_InkOverlay, rclsid))
    {
        return IClassFactory_QueryInterface(&cf_overlay, riid, ppv);
    }

    FIXME("Not implemented. Requested class was:%s\n", debugstr_guid(rclsid));

    return CLASS_E_CLASSNOTAVAILABLE;
}
