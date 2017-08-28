#include "includes.h"
#include "nucleas.h"
#include "sample.h"
#include "utils.h"


const NewClassDescr NC_STACK_sample::description("sample.class", &newinstance);


size_t NC_STACK_sample::func0(stack_vals *stak)
{
    if ( !NC_STACK_rsrc::func0(stak) )
        return 0;

    __NC_STACK_sample *smpl = &stack__sample;

    smpl->p_sampl = (sampl *)getRsrc_pData();

    return 1;
}

size_t NC_STACK_sample::func3(stack_vals *stak)
{
    stack_vals *stk = stak;

    while ( 1 )
    {
        if (stk->id == stack_vals::TAG_END)
            break;
        else if (stk->id == stack_vals::TAG_PTAGS)
        {
            stk = (stack_vals *)stk->value.p_data;
        }
        else if ( stk->id == stack_vals::TAG_SKIP_N )
        {
            stk += stk->value.i_data;
            ////a2++; ////BUGFIX?
        }
        else
        {
            switch ( stk->id )
            {
            default:
                break;

            case SMPL_ATT_PSAMPLE:
                *(sampl **)stk->value.p_data = getSMPL_pSample();
                break;
            case SMPL_ATT_TYPE:
                *(int *)stk->value.p_data = getSMPL_type();
                break;
            case SMPL_ATT_LEN:
                *(int *)stk->value.p_data = getSMPL_len();
                break;
            case SMPL_ATT_BUFFER:
                *(void **)stk->value.p_data = getSMPL_buffer();
                break;
            }
            stk++;
        }
    }

    return NC_STACK_rsrc::func3(stak);
}


rsrc * NC_STACK_sample::rsrc_func64(stack_vals *stak)
{
    rsrc *res = NC_STACK_rsrc::rsrc_func64(stak);

    if ( !res )
        return NULL;

    int bufsz = find_id_in_stack_def_val(SMPL_ATT_LEN, 0, stak);
    int type = find_id_in_stack_def_val(SMPL_ATT_TYPE, 0xFFFF, stak);

    if ( bufsz == 0 || type == 0xFFFF )
        return res;

    sampl *smpl = (sampl *)AllocVec(sizeof(sampl), 65537);

    if ( !smpl )
        return res;

    smpl->bufsz = bufsz;
    smpl->field_8 = type;

    void *buf = (void *)find_id_pval(SMPL_ATT_BUFFER, stak);

    if ( !buf )
    {
        buf = AllocVec(bufsz, 65537);
        smpl->sample_buffer = buf;

        if ( !buf )
        {
            nc_FreeMem(smpl);
            return res;
        }
    }
    else
    {
        smpl->sample_buffer = buf;
        smpl->field_10 |= 1;
    }

    res->data = smpl;

    return res;
}

size_t NC_STACK_sample::rsrc_func65(rsrc *res)
{
    sampl *smpl = (sampl *)res->data;

    if ( smpl )
    {
        if ( !(smpl->field_10 & 1) )
        {
            if ( smpl->sample_buffer )
                nc_FreeMem(smpl->sample_buffer);
        }
        nc_FreeMem(smpl);
        res->data = NULL;
    }

    return NC_STACK_rsrc::rsrc_func65(res);
}

void * NC_STACK_sample::sample_func128(void **arg)
{
    printf("%s - NOT RECOGINZED ARGUMENT\n","sample_func128");
    sampl *smpl = stack__sample.p_sampl;
    arg[2] = smpl;
    return smpl;
}



sampl *NC_STACK_sample::getSMPL_pSample()
{
    return stack__sample.p_sampl;
}

int NC_STACK_sample::getSMPL_type()
{
    if (stack__sample.p_sampl)
        return stack__sample.p_sampl->field_8;
    return 0;
}

int NC_STACK_sample::getSMPL_len()
{
    if (stack__sample.p_sampl)
        return stack__sample.p_sampl->bufsz;
    return 0;
}

void *NC_STACK_sample::getSMPL_buffer()
{
    if (stack__sample.p_sampl)
        return stack__sample.p_sampl->sample_buffer;
    return 0;
}


size_t NC_STACK_sample::compatcall(int method_id, void *data)
{
    switch( method_id )
    {
    case 0:
        return (size_t)func0( (stack_vals *)data );
    case 3:
        return func3( (stack_vals *)data );
    case 64:
        return (size_t)rsrc_func64( (stack_vals *)data );
    case 65:
        return rsrc_func65( (rsrc *)data );
    case 128:
        return (size_t)sample_func128( (void **)data );
    default:
        break;
    }
    return NC_STACK_rsrc::compatcall(method_id, data);
}
