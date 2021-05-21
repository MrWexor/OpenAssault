#include "includes.h"
#include "nucleas.h"
#include "rsrc.h"
#include "bitmap.h"
#include "utils.h"


const Nucleus::ClassDescr NC_STACK_bitmap::description("bitmap.class", &newinstance);

int NC_STACK_bitmap::sub_416704(pixel_2d *src)
{
    //// WHAT IT THIS !?
    int a4 = getRsrc_dontCopy();

    if ( outline_coords )
    {
        if ( !a4 )
            nc_FreeMem(outline_coords);
    }

    if ( a4 )
    {
        outline_coords = (tUtV *)src;
        return 1;
    }

    int opl_count = 1; //Mandatory end (tu/tv = -1/-1)

    pixel_2d *opl_cur = src;

    while (opl_cur->flags >= 0)
    {
        opl_count++;
        opl_cur++;
    }

    tUtV *unk = (tUtV *)AllocVec(sizeof(tUtV) * opl_count, 1);

    if ( unk )
    {
        tUtV *tmp = unk;
        pixel_2d *opl = src;

        for (int i = 0; i < (opl_count - 1); i++)
        {
            tmp[i].tu = (float)opl[i].x * (1.0 / 256.0);
            tmp[i].tv = (float)opl[i].y * (1.0 / 256.0);
        }

        tmp[opl_count - 1].tu = -1;
        tmp[opl_count - 1].tv = -1;

        outline_coords = unk;

        return 1;
    }
    return 0;
}


size_t NC_STACK_bitmap::Init(IDVList &stak)
{
    if ( !NC_STACK_rsrc::Init(stak) )
        return 0;

    pixel_2d *v9 = stak.Get<pixel_2d *>(BMD_ATT_OUTLINE, NULL);

    if ( v9 )
        sub_416704(v9);

    bitm_intern = (ResBitmap *)getRsrc_pData();

    return 1;
}

size_t NC_STACK_bitmap::Deinit()
{
    if ( outline_coords )
    {
        size_t a4 = getRsrc_dontCopy();

        if ( !a4 )
            nc_FreeMem(outline_coords);
    }

    return NC_STACK_rsrc::Deinit();
}

// Create bitmap resource node and fill rsrc field data
rsrc * NC_STACK_bitmap::rsrc_func64(IDVList &stak)
{
    rsrc *res = NC_STACK_rsrc::rsrc_func64(stak);// rsrc_func64
    if ( res )
    {
        int width = stak.Get<int32_t>(BMD_ATT_WIDTH, 0);
        int height = stak.Get<int32_t>(BMD_ATT_HEIGHT, 0);
        int colormap = stak.Get<int32_t>(BMD_ATT_HAS_COLORMAP, 0);
        //int create_texture = stak.Get<int32_t>(BMD_ATT_TEXTURE, 0);


        if ( width && height )
        {
            ResBitmap *intern = new ResBitmap;

            if ( intern )
            {
                if (colormap)
                    intern->palette = new UA_PALETTE;

                intern->width = width;
                intern->height = height;

                if ( !colormap )
                {
                    intern->width = width;
                    intern->height = height;

                    // allocate buffer, create palette, surface and texture
                    intern->swTex = GFX::Engine.CreateSurfaceScreenFormat(width, height);
                    if (!intern->swTex)
                    {
                        delete intern;
                        return res;
                    }
                }
                else
                {
                    intern->swTex = SDL_CreateRGBSurface(0, width, height, 8, 0, 0, 0, 0);
                    if ( !intern->swTex )
                    {
                        delete intern;
                        return res;
                    }
                }
                res->data = intern;
            }
        }
    }
    return res;
}

size_t NC_STACK_bitmap::rsrc_func65(rsrc *res)
{
    ResBitmap *intern = (ResBitmap *)(res)->data;

    if ( intern )
    {
        if ( intern->hwTex )
            GFX::Engine.FreeTexture(intern);

        if ( intern->swTex )
            SDL_FreeSurface(intern->swTex);

        if ( intern->palette )
            delete intern->palette;

        delete intern;
        res->data = NULL;
    }

    return NC_STACK_rsrc::rsrc_func65(res);
}

size_t NC_STACK_bitmap::bitmap_func128(IDVPair *)
{
    return 0;
}

size_t NC_STACK_bitmap::bitmap_func129(IDVPair *)
{
    return 0;
}

void NC_STACK_bitmap::bitmap_func130(bitmap_arg130 *out)
{
    out->pbitm = bitm_intern;
    out->outline = outline_coords;
}



void NC_STACK_bitmap::setBMD_outline(pixel_2d *otl)
{
    sub_416704(otl);
}

void NC_STACK_bitmap::setBMD_palette(UA_PALETTE *newPal)
{
    if ( bitm_intern->palette )
        *bitm_intern->palette = *newPal;
}



ResBitmap * NC_STACK_bitmap::GetResBmp()
{
    return bitm_intern;
}

int NC_STACK_bitmap::getBMD_width()
{
    if (bitm_intern)
        return bitm_intern->width;

    return 0;
}

int NC_STACK_bitmap::getBMD_height()
{
    if (bitm_intern)
        return bitm_intern->height;

    return 0;
}

int NC_STACK_bitmap::getBMD_hasPalette()
{
    if (bitm_intern)
        return bitm_intern->palette != NULL;

    return 0;
}

UA_PALETTE *NC_STACK_bitmap::getBMD_palette()
{
    if (bitm_intern)
        return bitm_intern->palette;

    return NULL;
}

SDL_Surface * NC_STACK_bitmap::GetSwTex()
{
    if (bitm_intern)
        return bitm_intern->swTex;

    return NULL;
}

void NC_STACK_bitmap::PrepareTexture( bool force )
{
    if (!bitm_intern)
        return;
    
    if (bitm_intern->hwTex && !force)
        return;
    
    if (bitm_intern->hwTex)
        GFX::Engine.FreeTexture(bitm_intern);
        
    GFX::Engine.AllocTexture(bitm_intern);
}

